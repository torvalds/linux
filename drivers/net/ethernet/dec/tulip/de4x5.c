/*  de4x5.c: A DIGITAL DC21x4x DECchip and DE425/DE434/DE435/DE450/DE500
             ethernet driver for Linux.

    Copyright 1994, 1995 Digital Equipment Corporation.

    Testing resources for this driver have been made available
    in part by NASA Ames Research Center (mjacob@nas.nasa.gov).

    The author may be reached at davies@maniac.ultranet.com.

    This program is free software; you can redistribute  it and/or modify it
    under  the terms of  the GNU General  Public License as published by the
    Free Software Foundation;  either version 2 of the  License, or (at your
    option) any later version.

    THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR   IMPLIED
    WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
    MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
    NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT,  INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
    NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
    USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
    ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
    THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    You should have received a copy of the  GNU General Public License along
    with this program; if not, write  to the Free Software Foundation, Inc.,
    675 Mass Ave, Cambridge, MA 02139, USA.

    Originally,   this  driver  was    written  for the  Digital   Equipment
    Corporation series of EtherWORKS ethernet cards:

        DE425 TP/COAX EISA
	DE434 TP PCI
	DE435 TP/COAX/AUI PCI
	DE450 TP/COAX/AUI PCI
	DE500 10/100 PCI Fasternet

    but it  will  now attempt  to  support all  cards which   conform to the
    Digital Semiconductor   SROM   Specification.    The  driver   currently
    recognises the following chips:

        DC21040  (no SROM)
	DC21041[A]
	DC21140[A]
	DC21142
	DC21143

    So far the driver is known to work with the following cards:

        KINGSTON
	Linksys
	ZNYX342
	SMC8432
	SMC9332 (w/new SROM)
	ZNYX31[45]
	ZNYX346 10/100 4 port (can act as a 10/100 bridge!)

    The driver has been tested on a relatively busy network using the DE425,
    DE434, DE435 and DE500 cards and benchmarked with 'ttcp': it transferred
    16M of data to a DECstation 5000/200 as follows:

                TCP           UDP
             TX     RX     TX     RX
    DE425   1030k  997k   1170k  1128k
    DE434   1063k  995k   1170k  1125k
    DE435   1063k  995k   1170k  1125k
    DE500   1063k  998k   1170k  1125k  in 10Mb/s mode

    All  values are typical (in   kBytes/sec) from a  sample  of 4 for  each
    measurement. Their error is +/-20k on a quiet (private) network and also
    depend on what load the CPU has.

    =========================================================================
    This driver  has been written substantially  from  scratch, although its
    inheritance of style and stack interface from 'ewrk3.c' and in turn from
    Donald Becker's 'lance.c' should be obvious. With the module autoload of
    every  usable DECchip board,  I  pinched Donald's 'next_module' field to
    link my modules together.

    Up to 15 EISA cards can be supported under this driver, limited primarily
    by the available IRQ lines.  I have  checked different configurations of
    multiple depca, EtherWORKS 3 cards and de4x5 cards and  have not found a
    problem yet (provided you have at least depca.c v0.38) ...

    PCI support has been added  to allow the driver  to work with the DE434,
    DE435, DE450 and DE500 cards. The I/O accesses are a bit of a kludge due
    to the differences in the EISA and PCI CSR address offsets from the base
    address.

    The ability to load this  driver as a loadable  module has been included
    and used extensively  during the driver development  (to save those long
    reboot sequences).  Loadable module support  under PCI and EISA has been
    achieved by letting the driver autoprobe as if it were compiled into the
    kernel. Do make sure  you're not sharing  interrupts with anything  that
    cannot accommodate  interrupt  sharing!

    To utilise this ability, you have to do 8 things:

    0) have a copy of the loadable modules code installed on your system.
    1) copy de4x5.c from the  /linux/drivers/net directory to your favourite
    temporary directory.
    2) for fixed  autoprobes (not  recommended),  edit the source code  near
    line 5594 to reflect the I/O address  you're using, or assign these when
    loading by:

                   insmod de4x5 io=0xghh           where g = bus number
		                                        hh = device number

       NB: autoprobing for modules is now supported by default. You may just
           use:

                   insmod de4x5

           to load all available boards. For a specific board, still use
	   the 'io=?' above.
    3) compile  de4x5.c, but include -DMODULE in  the command line to ensure
    that the correct bits are compiled (see end of source code).
    4) if you are wanting to add a new  card, goto 5. Otherwise, recompile a
    kernel with the de4x5 configuration turned off and reboot.
    5) insmod de4x5 [io=0xghh]
    6) run the net startup bits for your new eth?? interface(s) manually
    (usually /etc/rc.inet[12] at boot time).
    7) enjoy!

    To unload a module, turn off the associated interface(s)
    'ifconfig eth?? down' then 'rmmod de4x5'.

    Automedia detection is included so that in  principal you can disconnect
    from, e.g.  TP, reconnect  to BNC  and  things will still work  (after a
    pause whilst the   driver figures out   where its media went).  My tests
    using ping showed that it appears to work....

    By  default,  the driver will  now   autodetect any  DECchip based card.
    Should you have a need to restrict the driver to DIGITAL only cards, you
    can compile with a  DEC_ONLY define, or if  loading as a module, use the
    'dec_only=1'  parameter.

    I've changed the timing routines to  use the kernel timer and scheduling
    functions  so that the  hangs  and other assorted problems that occurred
    while autosensing the  media  should be gone.  A  bonus  for the DC21040
    auto  media sense algorithm is  that it can now  use one that is more in
    line with the  rest (the DC21040  chip doesn't  have a hardware  timer).
    The downside is the 1 'jiffies' (10ms) resolution.

    IEEE 802.3u MII interface code has  been added in anticipation that some
    products may use it in the future.

    The SMC9332 card  has a non-compliant SROM  which needs fixing -  I have
    patched this  driver to detect it  because the SROM format used complies
    to a previous DEC-STD format.

    I have removed the buffer copies needed for receive on Intels.  I cannot
    remove them for   Alphas since  the  Tulip hardware   only does longword
    aligned  DMA transfers  and  the  Alphas get   alignment traps with  non
    longword aligned data copies (which makes them really slow). No comment.

    I  have added SROM decoding  routines to make this  driver work with any
    card that  supports the Digital  Semiconductor SROM spec. This will help
    all  cards running the dc2114x  series chips in particular.  Cards using
    the dc2104x  chips should run correctly with  the basic  driver.  I'm in
    debt to <mjacob@feral.com> for the  testing and feedback that helped get
    this feature working.  So far we have  tested KINGSTON, SMC8432, SMC9332
    (with the latest SROM complying  with the SROM spec  V3: their first was
    broken), ZNYX342  and  LinkSys. ZYNX314 (dual  21041  MAC) and  ZNYX 315
    (quad 21041 MAC)  cards also  appear  to work despite their  incorrectly
    wired IRQs.

    I have added a temporary fix for interrupt problems when some SCSI cards
    share the same interrupt as the DECchip based  cards. The problem occurs
    because  the SCSI card wants to  grab the interrupt  as a fast interrupt
    (runs the   service routine with interrupts turned   off) vs.  this card
    which really needs to run the service routine with interrupts turned on.
    This driver will  now   add the interrupt service   routine  as  a  fast
    interrupt if it   is bounced from the   slow interrupt.  THIS IS NOT   A
    RECOMMENDED WAY TO RUN THE DRIVER  and has been done  for a limited time
    until  people   sort  out their  compatibility    issues and the  kernel
    interrupt  service code  is  fixed.   YOU  SHOULD SEPARATE OUT  THE FAST
    INTERRUPT CARDS FROM THE SLOW INTERRUPT CARDS to ensure that they do not
    run on the same interrupt. PCMCIA/CardBus is another can of worms...

    Finally, I think  I have really  fixed  the module  loading problem with
    more than one DECchip based  card.  As a  side effect, I don't mess with
    the  device structure any  more which means that  if more than 1 card in
    2.0.x is    installed (4  in   2.1.x),  the  user   will have   to  edit
    linux/drivers/net/Space.c  to make room for  them. Hence, module loading
    is  the preferred way to use   this driver, since  it  doesn't have this
    limitation.

    Where SROM media  detection is used and  full duplex is specified in the
    SROM,  the feature is  ignored unless  lp->params.fdx  is set at compile
    time  OR during  a   module load  (insmod  de4x5   args='eth??:fdx' [see
    below]).  This is because there  is no way  to automatically detect full
    duplex   links  except through   autonegotiation.    When I  include the
    autonegotiation feature in  the SROM autoconf  code, this detection will
    occur automatically for that case.

    Command  line arguments are  now  allowed, similar  to passing arguments
    through LILO. This will allow a per adapter board  set up of full duplex
    and media. The only lexical constraints  are: the board name (dev->name)
    appears in the list before its  parameters.  The list of parameters ends
    either at the end of the parameter list or with another board name.  The
    following parameters are allowed:

            fdx        for full duplex
	    autosense  to set the media/speed; with the following
	               sub-parameters:
		       TP, TP_NW, BNC, AUI, BNC_AUI, 100Mb, 10Mb, AUTO

    Case sensitivity is important  for  the sub-parameters. They *must*   be
    upper case. Examples:

        insmod de4x5 args='eth1:fdx autosense=BNC eth0:autosense=100Mb'.

    For a compiled in driver, at or above line 548, place e.g.
	#define DE4X5_PARM "eth0:fdx autosense=AUI eth2:autosense=TP"

    Yes,  I know full duplex isn't  permissible on BNC  or AUI; they're just
    examples. By default, full duplex is turned off and  AUTO is the default
    autosense setting.  In reality, I expect only  the full duplex option to
    be used. Note the use of single quotes in the two examples above and the
    lack of commas to separate items. ALSO, you must get the requested media
    correct in relation to what the adapter SROM says it has. There's no way
    to  determine this in  advance other than by  trial and error and common
    sense, e.g. call a BNC connectored port 'BNC', not '10Mb'.

    Changed the bus probing.  EISA used to be  done first,  followed by PCI.
    Most people probably don't even know  what a de425 is today and the EISA
    probe has messed  up some SCSI cards  in the past,  so now PCI is always
    probed  first  followed by  EISA if  a) the architecture allows EISA and
    either  b) there have been no PCI cards detected or  c) an EISA probe is
    forced by  the user.  To force  a probe  include  "force_eisa"  in  your
    insmod "args" line;  for built-in kernels either change the driver to do
    this  automatically  or include  #define DE4X5_FORCE_EISA  on or  before
    line 1040 in the driver.

    TO DO:
    ------

    Revision History
    ----------------

    Version   Date        Description

      0.1     17-Nov-94   Initial writing. ALPHA code release.
      0.2     13-Jan-95   Added PCI support for DE435's.
      0.21    19-Jan-95   Added auto media detection.
      0.22    10-Feb-95   Fix interrupt handler call <chris@cosy.sbg.ac.at>.
                          Fix recognition bug reported by <bkm@star.rl.ac.uk>.
			  Add request/release_region code.
			  Add loadable modules support for PCI.
			  Clean up loadable modules support.
      0.23    28-Feb-95   Added DC21041 and DC21140 support.
                          Fix missed frame counter value and initialisation.
			  Fixed EISA probe.
      0.24    11-Apr-95   Change delay routine to use <linux/udelay>.
                          Change TX_BUFFS_AVAIL macro.
			  Change media autodetection to allow manual setting.
			  Completed DE500 (DC21140) support.
      0.241   18-Apr-95   Interim release without DE500 Autosense Algorithm.
      0.242   10-May-95   Minor changes.
      0.30    12-Jun-95   Timer fix for DC21140.
                          Portability changes.
			  Add ALPHA changes from <jestabro@ant.tay1.dec.com>.
			  Add DE500 semi automatic autosense.
			  Add Link Fail interrupt TP failure detection.
			  Add timer based link change detection.
			  Plugged a memory leak in de4x5_queue_pkt().
      0.31    13-Jun-95   Fixed PCI stuff for 1.3.1.
      0.32    26-Jun-95   Added verify_area() calls in de4x5_ioctl() from a
                          suggestion by <heiko@colossus.escape.de>.
      0.33     8-Aug-95   Add shared interrupt support (not released yet).
      0.331   21-Aug-95   Fix de4x5_open() with fast CPUs.
                          Fix de4x5_interrupt().
                          Fix dc21140_autoconf() mess.
			  No shared interrupt support.
      0.332   11-Sep-95   Added MII management interface routines.
      0.40     5-Mar-96   Fix setup frame timeout <maartenb@hpkuipc.cern.ch>.
                          Add kernel timer code (h/w is too flaky).
			  Add MII based PHY autosense.
			  Add new multicasting code.
			  Add new autosense algorithms for media/mode
			  selection using kernel scheduling/timing.
			  Re-formatted.
			  Made changes suggested by <jeff@router.patch.net>:
			    Change driver to detect all DECchip based cards
			    with DEC_ONLY restriction a special case.
			    Changed driver to autoprobe as a module. No irq
			    checking is done now - assume BIOS is good!
			  Added SMC9332 detection <manabe@Roy.dsl.tutics.ac.jp>
      0.41    21-Mar-96   Don't check for get_hw_addr checksum unless DEC card
                          only <niles@axp745gsfc.nasa.gov>
			  Fix for multiple PCI cards reported by <jos@xos.nl>
			  Duh, put the IRQF_SHARED flag into request_interrupt().
			  Fix SMC ethernet address in enet_det[].
			  Print chip name instead of "UNKNOWN" during boot.
      0.42    26-Apr-96   Fix MII write TA bit error.
                          Fix bug in dc21040 and dc21041 autosense code.
			  Remove buffer copies on receive for Intels.
			  Change sk_buff handling during media disconnects to
			   eliminate DUP packets.
			  Add dynamic TX thresholding.
			  Change all chips to use perfect multicast filtering.
			  Fix alloc_device() bug <jari@markkus2.fimr.fi>
      0.43   21-Jun-96    Fix unconnected media TX retry bug.
                          Add Accton to the list of broken cards.
			  Fix TX under-run bug for non DC21140 chips.
			  Fix boot command probe bug in alloc_device() as
			   reported by <koen.gadeyne@barco.com> and
			   <orava@nether.tky.hut.fi>.
			  Add cache locks to prevent a race condition as
			   reported by <csd@microplex.com> and
			   <baba@beckman.uiuc.edu>.
			  Upgraded alloc_device() code.
      0.431  28-Jun-96    Fix potential bug in queue_pkt() from discussion
                          with <csd@microplex.com>
      0.44   13-Aug-96    Fix RX overflow bug in 2114[023] chips.
                          Fix EISA probe bugs reported by <os2@kpi.kharkov.ua>
			  and <michael@compurex.com>.
      0.441   9-Sep-96    Change dc21041_autoconf() to probe quiet BNC media
                           with a loopback packet.
      0.442   9-Sep-96    Include AUI in dc21041 media printout. Bug reported
                           by <bhat@mundook.cs.mu.OZ.AU>
      0.45    8-Dec-96    Include endian functions for PPC use, from work
                           by <cort@cs.nmt.edu> and <g.thomas@opengroup.org>.
      0.451  28-Dec-96    Added fix to allow autoprobe for modules after
                           suggestion from <mjacob@feral.com>.
      0.5    30-Jan-97    Added SROM decoding functions.
                          Updated debug flags.
			  Fix sleep/wakeup calls for PCI cards, bug reported
			   by <cross@gweep.lkg.dec.com>.
			  Added multi-MAC, one SROM feature from discussion
			   with <mjacob@feral.com>.
			  Added full module autoprobe capability.
			  Added attempt to use an SMC9332 with broken SROM.
			  Added fix for ZYNX multi-mac cards that didn't
			   get their IRQs wired correctly.
      0.51   13-Feb-97    Added endian fixes for the SROM accesses from
			   <paubert@iram.es>
			  Fix init_connection() to remove extra device reset.
			  Fix MAC/PHY reset ordering in dc21140m_autoconf().
			  Fix initialisation problem with lp->timeout in
			   typeX_infoblock() from <paubert@iram.es>.
			  Fix MII PHY reset problem from work done by
			   <paubert@iram.es>.
      0.52   26-Apr-97    Some changes may not credit the right people -
                           a disk crash meant I lost some mail.
			  Change RX interrupt routine to drop rather than
			   defer packets to avoid hang reported by
			   <g.thomas@opengroup.org>.
			  Fix srom_exec() to return for COMPACT and type 1
			   infoblocks.
			  Added DC21142 and DC21143 functions.
			  Added byte counters from <phil@tazenda.demon.co.uk>
			  Added IRQF_DISABLED temporary fix from
			   <mjacob@feral.com>.
      0.53   12-Nov-97    Fix the *_probe() to include 'eth??' name during
                           module load: bug reported by
			   <Piete.Brooks@cl.cam.ac.uk>
			  Fix multi-MAC, one SROM, to work with 2114x chips:
			   bug reported by <cmetz@inner.net>.
			  Make above search independent of BIOS device scan
			   direction.
			  Completed DC2114[23] autosense functions.
      0.531  21-Dec-97    Fix DE500-XA 100Mb/s bug reported by
                           <robin@intercore.com
			  Fix type1_infoblock() bug introduced in 0.53, from
			   problem reports by
			   <parmee@postecss.ncrfran.france.ncr.com> and
			   <jo@ice.dillingen.baynet.de>.
			  Added argument list to set up each board from either
			   a module's command line or a compiled in #define.
			  Added generic MII PHY functionality to deal with
			   newer PHY chips.
			  Fix the mess in 2.1.67.
      0.532   5-Jan-98    Fix bug in mii_get_phy() reported by
                           <redhat@cococo.net>.
                          Fix bug in pci_probe() for 64 bit systems reported
			   by <belliott@accessone.com>.
      0.533   9-Jan-98    Fix more 64 bit bugs reported by <jal@cs.brown.edu>.
      0.534  24-Jan-98    Fix last (?) endian bug from <geert@linux-m68k.org>
      0.535  21-Feb-98    Fix Ethernet Address PROM reset bug for DC21040.
      0.536  21-Mar-98    Change pci_probe() to use the pci_dev structure.
			  **Incompatible with 2.0.x from here.**
      0.540   5-Jul-98    Atomicize assertion of dev->interrupt for SMP
                           from <lma@varesearch.com>
			  Add TP, AUI and BNC cases to 21140m_autoconf() for
			   case where a 21140 under SROM control uses, e.g. AUI
			   from problem report by <delchini@lpnp09.in2p3.fr>
			  Add MII parallel detection to 2114x_autoconf() for
			   case where no autonegotiation partner exists from
			   problem report by <mlapsley@ndirect.co.uk>.
			  Add ability to force connection type directly even
			   when using SROM control from problem report by
			   <earl@exis.net>.
			  Updated the PCI interface to conform with the latest
			   version. I hope nothing is broken...
          		  Add TX done interrupt modification from suggestion
			   by <Austin.Donnelly@cl.cam.ac.uk>.
			  Fix is_anc_capable() bug reported by
			   <Austin.Donnelly@cl.cam.ac.uk>.
			  Fix type[13]_infoblock() bug: during MII search, PHY
			   lp->rst not run because lp->ibn not initialised -
			   from report & fix by <paubert@iram.es>.
			  Fix probe bug with EISA & PCI cards present from
                           report by <eirik@netcom.com>.
      0.541  24-Aug-98    Fix compiler problems associated with i386-string
                           ops from multiple bug reports and temporary fix
			   from <paubert@iram.es>.
			  Fix pci_probe() to correctly emulate the old
			   pcibios_find_class() function.
			  Add an_exception() for old ZYNX346 and fix compile
			   warning on PPC & SPARC, from <ecd@skynet.be>.
			  Fix lastPCI to correctly work with compiled in
			   kernels and modules from bug report by
			   <Zlatko.Calusic@CARNet.hr> et al.
      0.542  15-Sep-98    Fix dc2114x_autoconf() to stop multiple messages
                           when media is unconnected.
			  Change dev->interrupt to lp->interrupt to ensure
			   alignment for Alpha's and avoid their unaligned
			   access traps. This flag is merely for log messages:
			   should do something more definitive though...
      0.543  30-Dec-98    Add SMP spin locking.
      0.544   8-May-99    Fix for buggy SROM in Motorola embedded boards using
                           a 21143 by <mmporter@home.com>.
			  Change PCI/EISA bus probing order.
      0.545  28-Nov-99    Further Moto SROM bug fix from
                           <mporter@eng.mcd.mot.com>
                          Remove double checking for DEBUG_RX in de4x5_dbg_rx()
			   from report by <geert@linux-m68k.org>
      0.546  22-Feb-01    Fixes Alpha XP1000 oops.  The srom_search function
                           was causing a page fault when initializing the
                           variable 'pb', on a non de4x5 PCI device, in this
                           case a PCI bridge (DEC chip 21152). The value of
                           'pb' is now only initialized if a de4x5 chip is
                           present.
                           <france@handhelds.org>
      0.547  08-Nov-01    Use library crc32 functions by <Matt_Domsch@dell.com>
      0.548  30-Aug-03    Big 2.6 cleanup. Ported to PCI/EISA probing and
                           generic DMA APIs. Fixed DE425 support on Alpha.
			   <maz@wild-wind.fr.eu.org>
    =========================================================================
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/eisa.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/crc32.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/unistd.h>
#include <linux/ctype.h>
#include <linux/dma-mapping.h>
#include <linux/moduleparam.h>
#include <linux/bitops.h>
#include <linux/gfp.h>

#include <asm/io.h>
#include <asm/dma.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>
#include <linux/uaccess.h>
#ifdef CONFIG_PPC_PMAC
#include <asm/machdep.h>
#endif /* CONFIG_PPC_PMAC */

#include "de4x5.h"

static const char version[] =
	KERN_INFO "de4x5.c:V0.546 2001/02/22 davies@maniac.ultranet.com\n";

#define c_char const char

/*
** MII Information
*/
struct phy_table {
    int reset;              /* Hard reset required?                         */
    int id;                 /* IEEE OUI                                     */
    int ta;                 /* One cycle TA time - 802.3u is confusing here */
    struct {                /* Non autonegotiation (parallel) speed det.    */
	int reg;
	int mask;
	int value;
    } spd;
};

struct mii_phy {
    int reset;              /* Hard reset required?                      */
    int id;                 /* IEEE OUI                                  */
    int ta;                 /* One cycle TA time                         */
    struct {                /* Non autonegotiation (parallel) speed det. */
	int reg;
	int mask;
	int value;
    } spd;
    int addr;               /* MII address for the PHY                   */
    u_char  *gep;           /* Start of GEP sequence block in SROM       */
    u_char  *rst;           /* Start of reset sequence in SROM           */
    u_int mc;               /* Media Capabilities                        */
    u_int ana;              /* NWay Advertisement                        */
    u_int fdx;              /* Full DupleX capabilities for each media   */
    u_int ttm;              /* Transmit Threshold Mode for each media    */
    u_int mci;              /* 21142 MII Connector Interrupt info        */
};

#define DE4X5_MAX_PHY 8     /* Allow up to 8 attached PHY devices per board */

struct sia_phy {
    u_char mc;              /* Media Code                                */
    u_char ext;             /* csr13-15 valid when set                   */
    int csr13;              /* SIA Connectivity Register                 */
    int csr14;              /* SIA TX/RX Register                        */
    int csr15;              /* SIA General Register                      */
    int gepc;               /* SIA GEP Control Information               */
    int gep;                /* SIA GEP Data                              */
};

/*
** Define the know universe of PHY devices that can be
** recognised by this driver.
*/
static struct phy_table phy_info[] = {
    {0, NATIONAL_TX, 1, {0x19, 0x40, 0x00}},       /* National TX      */
    {1, BROADCOM_T4, 1, {0x10, 0x02, 0x02}},       /* Broadcom T4      */
    {0, SEEQ_T4    , 1, {0x12, 0x10, 0x10}},       /* SEEQ T4          */
    {0, CYPRESS_T4 , 1, {0x05, 0x20, 0x20}},       /* Cypress T4       */
    {0, 0x7810     , 1, {0x14, 0x0800, 0x0800}}    /* Level One LTX970 */
};

/*
** These GENERIC values assumes that the PHY devices follow 802.3u and
** allow parallel detection to set the link partner ability register.
** Detection of 100Base-TX [H/F Duplex] and 100Base-T4 is supported.
*/
#define GENERIC_REG   0x05      /* Autoneg. Link Partner Advertisement Reg. */
#define GENERIC_MASK  MII_ANLPA_100M /* All 100Mb/s Technologies            */
#define GENERIC_VALUE MII_ANLPA_100M /* 100B-TX, 100B-TX FDX, 100B-T4       */

/*
** Define special SROM detection cases
*/
static c_char enet_det[][ETH_ALEN] = {
    {0x00, 0x00, 0xc0, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0xe8, 0x00, 0x00, 0x00}
};

#define SMC    1
#define ACCTON 2

/*
** SROM Repair definitions. If a broken SROM is detected a card may
** use this information to help figure out what to do. This is a
** "stab in the dark" and so far for SMC9332's only.
*/
static c_char srom_repair_info[][100] = {
    {0x00,0x1e,0x00,0x00,0x00,0x08,             /* SMC9332 */
     0x1f,0x01,0x8f,0x01,0x00,0x01,0x00,0x02,
     0x01,0x00,0x00,0x78,0xe0,0x01,0x00,0x50,
     0x00,0x18,}
};


#ifdef DE4X5_DEBUG
static int de4x5_debug = DE4X5_DEBUG;
#else
/*static int de4x5_debug = (DEBUG_MII | DEBUG_SROM | DEBUG_PCICFG | DEBUG_MEDIA | DEBUG_VERSION);*/
static int de4x5_debug = (DEBUG_MEDIA | DEBUG_VERSION);
#endif

/*
** Allow per adapter set up. For modules this is simply a command line
** parameter, e.g.:
** insmod de4x5 args='eth1:fdx autosense=BNC eth0:autosense=100Mb'.
**
** For a compiled in driver, place e.g.
**     #define DE4X5_PARM "eth0:fdx autosense=AUI eth2:autosense=TP"
** here
*/
#ifdef DE4X5_PARM
static char *args = DE4X5_PARM;
#else
static char *args;
#endif

struct parameters {
    bool fdx;
    int autosense;
};

#define DE4X5_AUTOSENSE_MS 250      /* msec autosense tick (DE500) */

#define DE4X5_NDA 0xffe0            /* No Device (I/O) Address */

/*
** Ethernet PROM defines
*/
#define PROBE_LENGTH    32
#define ETH_PROM_SIG    0xAA5500FFUL

/*
** Ethernet Info
*/
#define PKT_BUF_SZ	1536            /* Buffer size for each Tx/Rx buffer */
#define IEEE802_3_SZ    1518            /* Packet + CRC */
#define MAX_PKT_SZ   	1514            /* Maximum ethernet packet length */
#define MAX_DAT_SZ   	1500            /* Maximum ethernet data length */
#define MIN_DAT_SZ   	1               /* Minimum ethernet data length */
#define PKT_HDR_LEN     14              /* Addresses and data length info */
#define FAKE_FRAME_LEN  (MAX_PKT_SZ + 1)
#define QUEUE_PKT_TIMEOUT (3*HZ)        /* 3 second timeout */


/*
** EISA bus defines
*/
#define DE4X5_EISA_IO_PORTS   0x0c00    /* I/O port base address, slot 0 */
#define DE4X5_EISA_TOTAL_SIZE 0x100     /* I/O address extent */

#define EISA_ALLOWED_IRQ_LIST  {5, 9, 10, 11}

#define DE4X5_SIGNATURE {"DE425","DE434","DE435","DE450","DE500"}
#define DE4X5_NAME_LENGTH 8

static c_char *de4x5_signatures[] = DE4X5_SIGNATURE;

/*
** Ethernet PROM defines for DC21040
*/
#define PROBE_LENGTH    32
#define ETH_PROM_SIG    0xAA5500FFUL

/*
** PCI Bus defines
*/
#define PCI_MAX_BUS_NUM      8
#define DE4X5_PCI_TOTAL_SIZE 0x80       /* I/O address extent */
#define DE4X5_CLASS_CODE     0x00020000 /* Network controller, Ethernet */

/*
** Memory Alignment. Each descriptor is 4 longwords long. To force a
** particular alignment on the TX descriptor, adjust DESC_SKIP_LEN and
** DESC_ALIGN. ALIGN aligns the start address of the private memory area
** and hence the RX descriptor ring's first entry.
*/
#define DE4X5_ALIGN4      ((u_long)4 - 1)     /* 1 longword align */
#define DE4X5_ALIGN8      ((u_long)8 - 1)     /* 2 longword align */
#define DE4X5_ALIGN16     ((u_long)16 - 1)    /* 4 longword align */
#define DE4X5_ALIGN32     ((u_long)32 - 1)    /* 8 longword align */
#define DE4X5_ALIGN64     ((u_long)64 - 1)    /* 16 longword align */
#define DE4X5_ALIGN128    ((u_long)128 - 1)   /* 32 longword align */

#define DE4X5_ALIGN         DE4X5_ALIGN32           /* Keep the DC21040 happy... */
#define DE4X5_CACHE_ALIGN   CAL_16LONG
#define DESC_SKIP_LEN DSL_0             /* Must agree with DESC_ALIGN */
/*#define DESC_ALIGN    u32 dummy[4];  / * Must agree with DESC_SKIP_LEN */
#define DESC_ALIGN

#ifndef DEC_ONLY                        /* See README.de4x5 for using this */
static int dec_only;
#else
static int dec_only = 1;
#endif

/*
** DE4X5 IRQ ENABLE/DISABLE
*/
#define ENABLE_IRQs { \
    imr |= lp->irq_en;\
    outl(imr, DE4X5_IMR);               /* Enable the IRQs */\
}

#define DISABLE_IRQs {\
    imr = inl(DE4X5_IMR);\
    imr &= ~lp->irq_en;\
    outl(imr, DE4X5_IMR);               /* Disable the IRQs */\
}

#define UNMASK_IRQs {\
    imr |= lp->irq_mask;\
    outl(imr, DE4X5_IMR);               /* Unmask the IRQs */\
}

#define MASK_IRQs {\
    imr = inl(DE4X5_IMR);\
    imr &= ~lp->irq_mask;\
    outl(imr, DE4X5_IMR);               /* Mask the IRQs */\
}

/*
** DE4X5 START/STOP
*/
#define START_DE4X5 {\
    omr = inl(DE4X5_OMR);\
    omr |= OMR_ST | OMR_SR;\
    outl(omr, DE4X5_OMR);               /* Enable the TX and/or RX */\
}

#define STOP_DE4X5 {\
    omr = inl(DE4X5_OMR);\
    omr &= ~(OMR_ST|OMR_SR);\
    outl(omr, DE4X5_OMR);               /* Disable the TX and/or RX */ \
}

/*
** DE4X5 SIA RESET
*/
#define RESET_SIA outl(0, DE4X5_SICR);  /* Reset SIA connectivity regs */

/*
** DE500 AUTOSENSE TIMER INTERVAL (MILLISECS)
*/
#define DE4X5_AUTOSENSE_MS  250

/*
** SROM Structure
*/
struct de4x5_srom {
    char sub_vendor_id[2];
    char sub_system_id[2];
    char reserved[12];
    char id_block_crc;
    char reserved2;
    char version;
    char num_controllers;
    char ieee_addr[6];
    char info[100];
    short chksum;
};
#define SUB_VENDOR_ID 0x500a

/*
** DE4X5 Descriptors. Make sure that all the RX buffers are contiguous
** and have sizes of both a power of 2 and a multiple of 4.
** A size of 256 bytes for each buffer could be chosen because over 90% of
** all packets in our network are <256 bytes long and 64 longword alignment
** is possible. 1536 showed better 'ttcp' performance. Take your pick. 32 TX
** descriptors are needed for machines with an ALPHA CPU.
*/
#define NUM_RX_DESC 8                   /* Number of RX descriptors   */
#define NUM_TX_DESC 32                  /* Number of TX descriptors   */
#define RX_BUFF_SZ  1536                /* Power of 2 for kmalloc and */
                                        /* Multiple of 4 for DC21040  */
                                        /* Allows 512 byte alignment  */
struct de4x5_desc {
    volatile __le32 status;
    __le32 des1;
    __le32 buf;
    __le32 next;
    DESC_ALIGN
};

/*
** The DE4X5 private structure
*/
#define DE4X5_PKT_STAT_SZ 16
#define DE4X5_PKT_BIN_SZ  128            /* Should be >=100 unless you
                                            increase DE4X5_PKT_STAT_SZ */

struct pkt_stats {
	u_int bins[DE4X5_PKT_STAT_SZ];      /* Private stats counters       */
	u_int unicast;
	u_int multicast;
	u_int broadcast;
	u_int excessive_collisions;
	u_int tx_underruns;
	u_int excessive_underruns;
	u_int rx_runt_frames;
	u_int rx_collision;
	u_int rx_dribble;
	u_int rx_overflow;
};

struct de4x5_private {
    char adapter_name[80];                  /* Adapter name                 */
    u_long interrupt;                       /* Aligned ISR flag             */
    struct de4x5_desc *rx_ring;		    /* RX descriptor ring           */
    struct de4x5_desc *tx_ring;		    /* TX descriptor ring           */
    struct sk_buff *tx_skb[NUM_TX_DESC];    /* TX skb for freeing when sent */
    struct sk_buff *rx_skb[NUM_RX_DESC];    /* RX skb's                     */
    int rx_new, rx_old;                     /* RX descriptor ring pointers  */
    int tx_new, tx_old;                     /* TX descriptor ring pointers  */
    char setup_frame[SETUP_FRAME_LEN];      /* Holds MCA and PA info.       */
    char frame[64];                         /* Min sized packet for loopback*/
    spinlock_t lock;                        /* Adapter specific spinlock    */
    struct net_device_stats stats;          /* Public stats                 */
    struct pkt_stats pktStats;	            /* Private stats counters	    */
    char rxRingSize;
    char txRingSize;
    int  bus;                               /* EISA or PCI                  */
    int  bus_num;                           /* PCI Bus number               */
    int  device;                            /* Device number on PCI bus     */
    int  state;                             /* Adapter OPENED or CLOSED     */
    int  chipset;                           /* DC21040, DC21041 or DC21140  */
    s32  irq_mask;                          /* Interrupt Mask (Enable) bits */
    s32  irq_en;                            /* Summary interrupt bits       */
    int  media;                             /* Media (eg TP), mode (eg 100B)*/
    int  c_media;                           /* Remember the last media conn */
    bool fdx;                               /* media full duplex flag       */
    int  linkOK;                            /* Link is OK                   */
    int  autosense;                         /* Allow/disallow autosensing   */
    bool tx_enable;                         /* Enable descriptor polling    */
    int  setup_f;                           /* Setup frame filtering type   */
    int  local_state;                       /* State within a 'media' state */
    struct mii_phy phy[DE4X5_MAX_PHY];      /* List of attached PHY devices */
    struct sia_phy sia;                     /* SIA PHY Information          */
    int  active;                            /* Index to active PHY device   */
    int  mii_cnt;                           /* Number of attached PHY's     */
    int  timeout;                           /* Scheduling counter           */
    struct timer_list timer;                /* Timer info for kernel        */
    int tmp;                                /* Temporary global per card    */
    struct {
	u_long lock;                        /* Lock the cache accesses      */
	s32 csr0;                           /* Saved Bus Mode Register      */
	s32 csr6;                           /* Saved Operating Mode Reg.    */
	s32 csr7;                           /* Saved IRQ Mask Register      */
	s32 gep;                            /* Saved General Purpose Reg.   */
	s32 gepc;                           /* Control info for GEP         */
	s32 csr13;                          /* Saved SIA Connectivity Reg.  */
	s32 csr14;                          /* Saved SIA TX/RX Register     */
	s32 csr15;                          /* Saved SIA General Register   */
	int save_cnt;                       /* Flag if state already saved  */
	struct sk_buff_head queue;          /* Save the (re-ordered) skb's  */
    } cache;
    struct de4x5_srom srom;                 /* A copy of the SROM           */
    int cfrv;				    /* Card CFRV copy */
    int rx_ovf;                             /* Check for 'RX overflow' tag  */
    bool useSROM;                           /* For non-DEC card use SROM    */
    bool useMII;                            /* Infoblock using the MII      */
    int asBitValid;                         /* Autosense bits in GEP?       */
    int asPolarity;                         /* 0 => asserted high           */
    int asBit;                              /* Autosense bit number in GEP  */
    int defMedium;                          /* SROM default medium          */
    int tcount;                             /* Last infoblock number        */
    int infoblock_init;                     /* Initialised this infoblock?  */
    int infoleaf_offset;                    /* SROM infoleaf for controller */
    s32 infoblock_csr6;                     /* csr6 value in SROM infoblock */
    int infoblock_media;                    /* infoblock media              */
    int (*infoleaf_fn)(struct net_device *);    /* Pointer to infoleaf function */
    u_char *rst;                            /* Pointer to Type 5 reset info */
    u_char  ibn;                            /* Infoblock number             */
    struct parameters params;               /* Command line/ #defined params */
    struct device *gendev;	            /* Generic device */
    dma_addr_t dma_rings;		    /* DMA handle for rings	    */
    int dma_size;			    /* Size of the DMA area	    */
    char *rx_bufs;			    /* rx bufs on alpha, sparc, ... */
};

/*
** To get around certain poxy cards that don't provide an SROM
** for the second and more DECchip, I have to key off the first
** chip's address. I'll assume there's not a bad SROM iff:
**
**      o the chipset is the same
**      o the bus number is the same and > 0
**      o the sum of all the returned hw address bytes is 0 or 0x5fa
**
** Also have to save the irq for those cards whose hardware designers
** can't follow the PCI to PCI Bridge Architecture spec.
*/
static struct {
    int chipset;
    int bus;
    int irq;
    u_char addr[ETH_ALEN];
} last = {0,};

/*
** The transmit ring full condition is described by the tx_old and tx_new
** pointers by:
**    tx_old            = tx_new    Empty ring
**    tx_old            = tx_new+1  Full ring
**    tx_old+txRingSize = tx_new+1  Full ring  (wrapped condition)
*/
#define TX_BUFFS_AVAIL ((lp->tx_old<=lp->tx_new)?\
			lp->tx_old+lp->txRingSize-lp->tx_new-1:\
			lp->tx_old               -lp->tx_new-1)

#define TX_PKT_PENDING (lp->tx_old != lp->tx_new)

/*
** Public Functions
*/
static int     de4x5_open(struct net_device *dev);
static netdev_tx_t de4x5_queue_pkt(struct sk_buff *skb,
					 struct net_device *dev);
static irqreturn_t de4x5_interrupt(int irq, void *dev_id);
static int     de4x5_close(struct net_device *dev);
static struct  net_device_stats *de4x5_get_stats(struct net_device *dev);
static void    de4x5_local_stats(struct net_device *dev, char *buf, int pkt_len);
static void    set_multicast_list(struct net_device *dev);
static int     de4x5_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);

/*
** Private functions
*/
static int     de4x5_hw_init(struct net_device *dev, u_long iobase, struct device *gendev);
static int     de4x5_init(struct net_device *dev);
static int     de4x5_sw_reset(struct net_device *dev);
static int     de4x5_rx(struct net_device *dev);
static int     de4x5_tx(struct net_device *dev);
static void    de4x5_ast(struct net_device *dev);
static int     de4x5_txur(struct net_device *dev);
static int     de4x5_rx_ovfc(struct net_device *dev);

static int     autoconf_media(struct net_device *dev);
static void    create_packet(struct net_device *dev, char *frame, int len);
static void    load_packet(struct net_device *dev, char *buf, u32 flags, struct sk_buff *skb);
static int     dc21040_autoconf(struct net_device *dev);
static int     dc21041_autoconf(struct net_device *dev);
static int     dc21140m_autoconf(struct net_device *dev);
static int     dc2114x_autoconf(struct net_device *dev);
static int     srom_autoconf(struct net_device *dev);
static int     de4x5_suspect_state(struct net_device *dev, int timeout, int prev_state, int (*fn)(struct net_device *, int), int (*asfn)(struct net_device *));
static int     dc21040_state(struct net_device *dev, int csr13, int csr14, int csr15, int timeout, int next_state, int suspect_state, int (*fn)(struct net_device *, int));
static int     test_media(struct net_device *dev, s32 irqs, s32 irq_mask, s32 csr13, s32 csr14, s32 csr15, s32 msec);
static int     test_for_100Mb(struct net_device *dev, int msec);
static int     wait_for_link(struct net_device *dev);
static int     test_mii_reg(struct net_device *dev, int reg, int mask, bool pol, long msec);
static int     is_spd_100(struct net_device *dev);
static int     is_100_up(struct net_device *dev);
static int     is_10_up(struct net_device *dev);
static int     is_anc_capable(struct net_device *dev);
static int     ping_media(struct net_device *dev, int msec);
static struct sk_buff *de4x5_alloc_rx_buff(struct net_device *dev, int index, int len);
static void    de4x5_free_rx_buffs(struct net_device *dev);
static void    de4x5_free_tx_buffs(struct net_device *dev);
static void    de4x5_save_skbs(struct net_device *dev);
static void    de4x5_rst_desc_ring(struct net_device *dev);
static void    de4x5_cache_state(struct net_device *dev, int flag);
static void    de4x5_put_cache(struct net_device *dev, struct sk_buff *skb);
static void    de4x5_putb_cache(struct net_device *dev, struct sk_buff *skb);
static struct  sk_buff *de4x5_get_cache(struct net_device *dev);
static void    de4x5_setup_intr(struct net_device *dev);
static void    de4x5_init_connection(struct net_device *dev);
static int     de4x5_reset_phy(struct net_device *dev);
static void    reset_init_sia(struct net_device *dev, s32 sicr, s32 strr, s32 sigr);
static int     test_ans(struct net_device *dev, s32 irqs, s32 irq_mask, s32 msec);
static int     test_tp(struct net_device *dev, s32 msec);
static int     EISA_signature(char *name, struct device *device);
static int     PCI_signature(char *name, struct de4x5_private *lp);
static void    DevicePresent(struct net_device *dev, u_long iobase);
static void    enet_addr_rst(u_long aprom_addr);
static int     de4x5_bad_srom(struct de4x5_private *lp);
static short   srom_rd(u_long address, u_char offset);
static void    srom_latch(u_int command, u_long address);
static void    srom_command(u_int command, u_long address);
static void    srom_address(u_int command, u_long address, u_char offset);
static short   srom_data(u_int command, u_long address);
/*static void    srom_busy(u_int command, u_long address);*/
static void    sendto_srom(u_int command, u_long addr);
static int     getfrom_srom(u_long addr);
static int     srom_map_media(struct net_device *dev);
static int     srom_infoleaf_info(struct net_device *dev);
static void    srom_init(struct net_device *dev);
static void    srom_exec(struct net_device *dev, u_char *p);
static int     mii_rd(u_char phyreg, u_char phyaddr, u_long ioaddr);
static void    mii_wr(int data, u_char phyreg, u_char phyaddr, u_long ioaddr);
static int     mii_rdata(u_long ioaddr);
static void    mii_wdata(int data, int len, u_long ioaddr);
static void    mii_ta(u_long rw, u_long ioaddr);
static int     mii_swap(int data, int len);
static void    mii_address(u_char addr, u_long ioaddr);
static void    sendto_mii(u32 command, int data, u_long ioaddr);
static int     getfrom_mii(u32 command, u_long ioaddr);
static int     mii_get_oui(u_char phyaddr, u_long ioaddr);
static int     mii_get_phy(struct net_device *dev);
static void    SetMulticastFilter(struct net_device *dev);
static int     get_hw_addr(struct net_device *dev);
static void    srom_repair(struct net_device *dev, int card);
static int     test_bad_enet(struct net_device *dev, int status);
static int     an_exception(struct de4x5_private *lp);
static char    *build_setup_frame(struct net_device *dev, int mode);
static void    disable_ast(struct net_device *dev);
static long    de4x5_switch_mac_port(struct net_device *dev);
static int     gep_rd(struct net_device *dev);
static void    gep_wr(s32 data, struct net_device *dev);
static void    yawn(struct net_device *dev, int state);
static void    de4x5_parse_params(struct net_device *dev);
static void    de4x5_dbg_open(struct net_device *dev);
static void    de4x5_dbg_mii(struct net_device *dev, int k);
static void    de4x5_dbg_media(struct net_device *dev);
static void    de4x5_dbg_srom(struct de4x5_srom *p);
static void    de4x5_dbg_rx(struct sk_buff *skb, int len);
static int     dc21041_infoleaf(struct net_device *dev);
static int     dc21140_infoleaf(struct net_device *dev);
static int     dc21142_infoleaf(struct net_device *dev);
static int     dc21143_infoleaf(struct net_device *dev);
static int     type0_infoblock(struct net_device *dev, u_char count, u_char *p);
static int     type1_infoblock(struct net_device *dev, u_char count, u_char *p);
static int     type2_infoblock(struct net_device *dev, u_char count, u_char *p);
static int     type3_infoblock(struct net_device *dev, u_char count, u_char *p);
static int     type4_infoblock(struct net_device *dev, u_char count, u_char *p);
static int     type5_infoblock(struct net_device *dev, u_char count, u_char *p);
static int     compact_infoblock(struct net_device *dev, u_char count, u_char *p);

/*
** Note now that module autoprobing is allowed under EISA and PCI. The
** IRQ lines will not be auto-detected; instead I'll rely on the BIOSes
** to "do the right thing".
*/

static int io=0x0;/* EDIT THIS LINE FOR YOUR CONFIGURATION IF NEEDED        */

module_param(io, int, 0);
module_param(de4x5_debug, int, 0);
module_param(dec_only, int, 0);
module_param(args, charp, 0);

MODULE_PARM_DESC(io, "de4x5 I/O base address");
MODULE_PARM_DESC(de4x5_debug, "de4x5 debug mask");
MODULE_PARM_DESC(dec_only, "de4x5 probe only for Digital boards (0-1)");
MODULE_PARM_DESC(args, "de4x5 full duplex and media type settings; see de4x5.c for details");
MODULE_LICENSE("GPL");

/*
** List the SROM infoleaf functions and chipsets
*/
struct InfoLeaf {
    int chipset;
    int (*fn)(struct net_device *);
};
static struct InfoLeaf infoleaf_array[] = {
    {DC21041, dc21041_infoleaf},
    {DC21140, dc21140_infoleaf},
    {DC21142, dc21142_infoleaf},
    {DC21143, dc21143_infoleaf}
};
#define INFOLEAF_SIZE ARRAY_SIZE(infoleaf_array)

/*
** List the SROM info block functions
*/
static int (*dc_infoblock[])(struct net_device *dev, u_char, u_char *) = {
    type0_infoblock,
    type1_infoblock,
    type2_infoblock,
    type3_infoblock,
    type4_infoblock,
    type5_infoblock,
    compact_infoblock
};

#define COMPACT (ARRAY_SIZE(dc_infoblock) - 1)

/*
** Miscellaneous defines...
*/
#define RESET_DE4X5 {\
    int i;\
    i=inl(DE4X5_BMR);\
    mdelay(1);\
    outl(i | BMR_SWR, DE4X5_BMR);\
    mdelay(1);\
    outl(i, DE4X5_BMR);\
    mdelay(1);\
    for (i=0;i<5;i++) {inl(DE4X5_BMR); mdelay(1);}\
    mdelay(1);\
}

#define PHY_HARD_RESET {\
    outl(GEP_HRST, DE4X5_GEP);           /* Hard RESET the PHY dev. */\
    mdelay(1);                           /* Assert for 1ms */\
    outl(0x00, DE4X5_GEP);\
    mdelay(2);                           /* Wait for 2ms */\
}

static const struct net_device_ops de4x5_netdev_ops = {
    .ndo_open		= de4x5_open,
    .ndo_stop		= de4x5_close,
    .ndo_start_xmit	= de4x5_queue_pkt,
    .ndo_get_stats	= de4x5_get_stats,
    .ndo_set_rx_mode	= set_multicast_list,
    .ndo_do_ioctl	= de4x5_ioctl,
    .ndo_set_mac_address= eth_mac_addr,
    .ndo_validate_addr	= eth_validate_addr,
};


static int
de4x5_hw_init(struct net_device *dev, u_long iobase, struct device *gendev)
{
    char name[DE4X5_NAME_LENGTH + 1];
    struct de4x5_private *lp = netdev_priv(dev);
    struct pci_dev *pdev = NULL;
    int i, status=0;

    dev_set_drvdata(gendev, dev);

    /* Ensure we're not sleeping */
    if (lp->bus == EISA) {
	outb(WAKEUP, PCI_CFPM);
    } else {
	pdev = to_pci_dev (gendev);
	pci_write_config_byte(pdev, PCI_CFDA_PSM, WAKEUP);
    }
    mdelay(10);

    RESET_DE4X5;

    if ((inl(DE4X5_STS) & (STS_TS | STS_RS)) != 0) {
	return -ENXIO;                       /* Hardware could not reset */
    }

    /*
    ** Now find out what kind of DC21040/DC21041/DC21140 board we have.
    */
    lp->useSROM = false;
    if (lp->bus == PCI) {
	PCI_signature(name, lp);
    } else {
	EISA_signature(name, gendev);
    }

    if (*name == '\0') {                     /* Not found a board signature */
	return -ENXIO;
    }

    dev->base_addr = iobase;
    printk ("%s: %s at 0x%04lx", dev_name(gendev), name, iobase);

    status = get_hw_addr(dev);
    printk(", h/w address %pM\n", dev->dev_addr);

    if (status != 0) {
	printk("      which has an Ethernet PROM CRC error.\n");
	return -ENXIO;
    } else {
	skb_queue_head_init(&lp->cache.queue);
	lp->cache.gepc = GEP_INIT;
	lp->asBit = GEP_SLNK;
	lp->asPolarity = GEP_SLNK;
	lp->asBitValid = ~0;
	lp->timeout = -1;
	lp->gendev = gendev;
	spin_lock_init(&lp->lock);
	init_timer(&lp->timer);
	lp->timer.function = (void (*)(unsigned long))de4x5_ast;
	lp->timer.data = (unsigned long)dev;
	de4x5_parse_params(dev);

	/*
	** Choose correct autosensing in case someone messed up
	*/
        lp->autosense = lp->params.autosense;
        if (lp->chipset != DC21140) {
            if ((lp->chipset==DC21040) && (lp->params.autosense&TP_NW)) {
                lp->params.autosense = TP;
            }
            if ((lp->chipset==DC21041) && (lp->params.autosense&BNC_AUI)) {
                lp->params.autosense = BNC;
            }
        }
	lp->fdx = lp->params.fdx;
	sprintf(lp->adapter_name,"%s (%s)", name, dev_name(gendev));

	lp->dma_size = (NUM_RX_DESC + NUM_TX_DESC) * sizeof(struct de4x5_desc);
#if defined(__alpha__) || defined(__powerpc__) || defined(CONFIG_SPARC) || defined(DE4X5_DO_MEMCPY)
	lp->dma_size += RX_BUFF_SZ * NUM_RX_DESC + DE4X5_ALIGN;
#endif
	lp->rx_ring = dma_alloc_coherent(gendev, lp->dma_size,
					 &lp->dma_rings, GFP_ATOMIC);
	if (lp->rx_ring == NULL) {
	    return -ENOMEM;
	}

	lp->tx_ring = lp->rx_ring + NUM_RX_DESC;

	/*
	** Set up the RX descriptor ring (Intels)
	** Allocate contiguous receive buffers, long word aligned (Alphas)
	*/
#if !defined(__alpha__) && !defined(__powerpc__) && !defined(CONFIG_SPARC) && !defined(DE4X5_DO_MEMCPY)
	for (i=0; i<NUM_RX_DESC; i++) {
	    lp->rx_ring[i].status = 0;
	    lp->rx_ring[i].des1 = cpu_to_le32(RX_BUFF_SZ);
	    lp->rx_ring[i].buf = 0;
	    lp->rx_ring[i].next = 0;
	    lp->rx_skb[i] = (struct sk_buff *) 1;     /* Dummy entry */
	}

#else
	{
		dma_addr_t dma_rx_bufs;

		dma_rx_bufs = lp->dma_rings + (NUM_RX_DESC + NUM_TX_DESC)
		      	* sizeof(struct de4x5_desc);
		dma_rx_bufs = (dma_rx_bufs + DE4X5_ALIGN) & ~DE4X5_ALIGN;
		lp->rx_bufs = (char *)(((long)(lp->rx_ring + NUM_RX_DESC
		      	+ NUM_TX_DESC) + DE4X5_ALIGN) & ~DE4X5_ALIGN);
		for (i=0; i<NUM_RX_DESC; i++) {
	    		lp->rx_ring[i].status = 0;
	    		lp->rx_ring[i].des1 = cpu_to_le32(RX_BUFF_SZ);
	    		lp->rx_ring[i].buf =
				cpu_to_le32(dma_rx_bufs+i*RX_BUFF_SZ);
	    		lp->rx_ring[i].next = 0;
	    		lp->rx_skb[i] = (struct sk_buff *) 1; /* Dummy entry */
		}

	}
#endif

	barrier();

	lp->rxRingSize = NUM_RX_DESC;
	lp->txRingSize = NUM_TX_DESC;

	/* Write the end of list marker to the descriptor lists */
	lp->rx_ring[lp->rxRingSize - 1].des1 |= cpu_to_le32(RD_RER);
	lp->tx_ring[lp->txRingSize - 1].des1 |= cpu_to_le32(TD_TER);

	/* Tell the adapter where the TX/RX rings are located. */
	outl(lp->dma_rings, DE4X5_RRBA);
	outl(lp->dma_rings + NUM_RX_DESC * sizeof(struct de4x5_desc),
	     DE4X5_TRBA);

	/* Initialise the IRQ mask and Enable/Disable */
	lp->irq_mask = IMR_RIM | IMR_TIM | IMR_TUM | IMR_UNM;
	lp->irq_en   = IMR_NIM | IMR_AIM;

	/* Create a loopback packet frame for later media probing */
	create_packet(dev, lp->frame, sizeof(lp->frame));

	/* Check if the RX overflow bug needs testing for */
	i = lp->cfrv & 0x000000fe;
	if ((lp->chipset == DC21140) && (i == 0x20)) {
	    lp->rx_ovf = 1;
	}

	/* Initialise the SROM pointers if possible */
	if (lp->useSROM) {
	    lp->state = INITIALISED;
	    if (srom_infoleaf_info(dev)) {
	        dma_free_coherent (gendev, lp->dma_size,
			       lp->rx_ring, lp->dma_rings);
		return -ENXIO;
	    }
	    srom_init(dev);
	}

	lp->state = CLOSED;

	/*
	** Check for an MII interface
	*/
	if ((lp->chipset != DC21040) && (lp->chipset != DC21041)) {
	    mii_get_phy(dev);
	}

	printk("      and requires IRQ%d (provided by %s).\n", dev->irq,
	       ((lp->bus == PCI) ? "PCI BIOS" : "EISA CNFG"));
    }

    if (de4x5_debug & DEBUG_VERSION) {
	printk(version);
    }

    /* The DE4X5-specific entries in the device structure. */
    SET_NETDEV_DEV(dev, gendev);
    dev->netdev_ops = &de4x5_netdev_ops;
    dev->mem_start = 0;

    /* Fill in the generic fields of the device structure. */
    if ((status = register_netdev (dev))) {
	    dma_free_coherent (gendev, lp->dma_size,
			       lp->rx_ring, lp->dma_rings);
	    return status;
    }

    /* Let the adapter sleep to save power */
    yawn(dev, SLEEP);

    return status;
}


static int
de4x5_open(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;
    int i, status = 0;
    s32 omr;

    /* Allocate the RX buffers */
    for (i=0; i<lp->rxRingSize; i++) {
	if (de4x5_alloc_rx_buff(dev, i, 0) == NULL) {
	    de4x5_free_rx_buffs(dev);
	    return -EAGAIN;
	}
    }

    /*
    ** Wake up the adapter
    */
    yawn(dev, WAKEUP);

    /*
    ** Re-initialize the DE4X5...
    */
    status = de4x5_init(dev);
    spin_lock_init(&lp->lock);
    lp->state = OPEN;
    de4x5_dbg_open(dev);

    if (request_irq(dev->irq, de4x5_interrupt, IRQF_SHARED,
		                                     lp->adapter_name, dev)) {
	printk("de4x5_open(): Requested IRQ%d is busy - attempting FAST/SHARE...", dev->irq);
	if (request_irq(dev->irq, de4x5_interrupt, IRQF_SHARED,
			                             lp->adapter_name, dev)) {
	    printk("\n              Cannot get IRQ- reconfigure your hardware.\n");
	    disable_ast(dev);
	    de4x5_free_rx_buffs(dev);
	    de4x5_free_tx_buffs(dev);
	    yawn(dev, SLEEP);
	    lp->state = CLOSED;
	    return -EAGAIN;
	} else {
	    printk("\n              Succeeded, but you should reconfigure your hardware to avoid this.\n");
	    printk("WARNING: there may be IRQ related problems in heavily loaded systems.\n");
	}
    }

    lp->interrupt = UNMASK_INTERRUPTS;
    netif_trans_update(dev); /* prevent tx timeout */

    START_DE4X5;

    de4x5_setup_intr(dev);

    if (de4x5_debug & DEBUG_OPEN) {
	printk("\tsts:  0x%08x\n", inl(DE4X5_STS));
	printk("\tbmr:  0x%08x\n", inl(DE4X5_BMR));
	printk("\timr:  0x%08x\n", inl(DE4X5_IMR));
	printk("\tomr:  0x%08x\n", inl(DE4X5_OMR));
	printk("\tsisr: 0x%08x\n", inl(DE4X5_SISR));
	printk("\tsicr: 0x%08x\n", inl(DE4X5_SICR));
	printk("\tstrr: 0x%08x\n", inl(DE4X5_STRR));
	printk("\tsigr: 0x%08x\n", inl(DE4X5_SIGR));
    }

    return status;
}

/*
** Initialize the DE4X5 operating conditions. NB: a chip problem with the
** DC21140 requires using perfect filtering mode for that chip. Since I can't
** see why I'd want > 14 multicast addresses, I have changed all chips to use
** the perfect filtering mode. Keep the DMA burst length at 8: there seems
** to be data corruption problems if it is larger (UDP errors seen from a
** ttcp source).
*/
static int
de4x5_init(struct net_device *dev)
{
    /* Lock out other processes whilst setting up the hardware */
    netif_stop_queue(dev);

    de4x5_sw_reset(dev);

    /* Autoconfigure the connected port */
    autoconf_media(dev);

    return 0;
}

static int
de4x5_sw_reset(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;
    int i, j, status = 0;
    s32 bmr, omr;

    /* Select the MII or SRL port now and RESET the MAC */
    if (!lp->useSROM) {
	if (lp->phy[lp->active].id != 0) {
	    lp->infoblock_csr6 = OMR_SDP | OMR_PS | OMR_HBD;
	} else {
	    lp->infoblock_csr6 = OMR_SDP | OMR_TTM;
	}
	de4x5_switch_mac_port(dev);
    }

    /*
    ** Set the programmable burst length to 8 longwords for all the DC21140
    ** Fasternet chips and 4 longwords for all others: DMA errors result
    ** without these values. Cache align 16 long.
    */
    bmr = (lp->chipset==DC21140 ? PBL_8 : PBL_4) | DESC_SKIP_LEN | DE4X5_CACHE_ALIGN;
    bmr |= ((lp->chipset & ~0x00ff)==DC2114x ? BMR_RML : 0);
    outl(bmr, DE4X5_BMR);

    omr = inl(DE4X5_OMR) & ~OMR_PR;             /* Turn off promiscuous mode */
    if (lp->chipset == DC21140) {
	omr |= (OMR_SDP | OMR_SB);
    }
    lp->setup_f = PERFECT;
    outl(lp->dma_rings, DE4X5_RRBA);
    outl(lp->dma_rings + NUM_RX_DESC * sizeof(struct de4x5_desc),
	 DE4X5_TRBA);

    lp->rx_new = lp->rx_old = 0;
    lp->tx_new = lp->tx_old = 0;

    for (i = 0; i < lp->rxRingSize; i++) {
	lp->rx_ring[i].status = cpu_to_le32(R_OWN);
    }

    for (i = 0; i < lp->txRingSize; i++) {
	lp->tx_ring[i].status = cpu_to_le32(0);
    }

    barrier();

    /* Build the setup frame depending on filtering mode */
    SetMulticastFilter(dev);

    load_packet(dev, lp->setup_frame, PERFECT_F|TD_SET|SETUP_FRAME_LEN, (struct sk_buff *)1);
    outl(omr|OMR_ST, DE4X5_OMR);

    /* Poll for setup frame completion (adapter interrupts are disabled now) */

    for (j=0, i=0;(i<500) && (j==0);i++) {       /* Up to 500ms delay */
	mdelay(1);
	if ((s32)le32_to_cpu(lp->tx_ring[lp->tx_new].status) >= 0) j=1;
    }
    outl(omr, DE4X5_OMR);                        /* Stop everything! */

    if (j == 0) {
	printk("%s: Setup frame timed out, status %08x\n", dev->name,
	       inl(DE4X5_STS));
	status = -EIO;
    }

    lp->tx_new = (lp->tx_new + 1) % lp->txRingSize;
    lp->tx_old = lp->tx_new;

    return status;
}

/*
** Writes a socket buffer address to the next available transmit descriptor.
*/
static netdev_tx_t
de4x5_queue_pkt(struct sk_buff *skb, struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;
    u_long flags = 0;

    netif_stop_queue(dev);
    if (!lp->tx_enable)                   /* Cannot send for now */
		goto tx_err;

    /*
    ** Clean out the TX ring asynchronously to interrupts - sometimes the
    ** interrupts are lost by delayed descriptor status updates relative to
    ** the irq assertion, especially with a busy PCI bus.
    */
    spin_lock_irqsave(&lp->lock, flags);
    de4x5_tx(dev);
    spin_unlock_irqrestore(&lp->lock, flags);

    /* Test if cache is already locked - requeue skb if so */
    if (test_and_set_bit(0, (void *)&lp->cache.lock) && !lp->interrupt)
		goto tx_err;

    /* Transmit descriptor ring full or stale skb */
    if (netif_queue_stopped(dev) || (u_long) lp->tx_skb[lp->tx_new] > 1) {
	if (lp->interrupt) {
	    de4x5_putb_cache(dev, skb);          /* Requeue the buffer */
	} else {
	    de4x5_put_cache(dev, skb);
	}
	if (de4x5_debug & DEBUG_TX) {
	    printk("%s: transmit busy, lost media or stale skb found:\n  STS:%08x\n  tbusy:%d\n  IMR:%08x\n  OMR:%08x\n Stale skb: %s\n",dev->name, inl(DE4X5_STS), netif_queue_stopped(dev), inl(DE4X5_IMR), inl(DE4X5_OMR), ((u_long) lp->tx_skb[lp->tx_new] > 1) ? "YES" : "NO");
	}
    } else if (skb->len > 0) {
	/* If we already have stuff queued locally, use that first */
	if (!skb_queue_empty(&lp->cache.queue) && !lp->interrupt) {
	    de4x5_put_cache(dev, skb);
	    skb = de4x5_get_cache(dev);
	}

	while (skb && !netif_queue_stopped(dev) &&
	       (u_long) lp->tx_skb[lp->tx_new] <= 1) {
	    spin_lock_irqsave(&lp->lock, flags);
	    netif_stop_queue(dev);
	    load_packet(dev, skb->data, TD_IC | TD_LS | TD_FS | skb->len, skb);
 	    lp->stats.tx_bytes += skb->len;
	    outl(POLL_DEMAND, DE4X5_TPD);/* Start the TX */

	    lp->tx_new = (lp->tx_new + 1) % lp->txRingSize;

	    if (TX_BUFFS_AVAIL) {
		netif_start_queue(dev);         /* Another pkt may be queued */
	    }
	    skb = de4x5_get_cache(dev);
	    spin_unlock_irqrestore(&lp->lock, flags);
	}
	if (skb) de4x5_putb_cache(dev, skb);
    }

    lp->cache.lock = 0;

    return NETDEV_TX_OK;
tx_err:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

/*
** The DE4X5 interrupt handler.
**
** I/O Read/Writes through intermediate PCI bridges are never 'posted',
** so that the asserted interrupt always has some real data to work with -
** if these I/O accesses are ever changed to memory accesses, ensure the
** STS write is read immediately to complete the transaction if the adapter
** is not on bus 0. Lost interrupts can still occur when the PCI bus load
** is high and descriptor status bits cannot be set before the associated
** interrupt is asserted and this routine entered.
*/
static irqreturn_t
de4x5_interrupt(int irq, void *dev_id)
{
    struct net_device *dev = dev_id;
    struct de4x5_private *lp;
    s32 imr, omr, sts, limit;
    u_long iobase;
    unsigned int handled = 0;

    lp = netdev_priv(dev);
    spin_lock(&lp->lock);
    iobase = dev->base_addr;

    DISABLE_IRQs;                        /* Ensure non re-entrancy */

    if (test_and_set_bit(MASK_INTERRUPTS, (void*) &lp->interrupt))
	printk("%s: Re-entering the interrupt handler.\n", dev->name);

    synchronize_irq(dev->irq);

    for (limit=0; limit<8; limit++) {
	sts = inl(DE4X5_STS);            /* Read IRQ status */
	outl(sts, DE4X5_STS);            /* Reset the board interrupts */

	if (!(sts & lp->irq_mask)) break;/* All done */
	handled = 1;

	if (sts & (STS_RI | STS_RU))     /* Rx interrupt (packet[s] arrived) */
	  de4x5_rx(dev);

	if (sts & (STS_TI | STS_TU))     /* Tx interrupt (packet sent) */
	  de4x5_tx(dev);

	if (sts & STS_LNF) {             /* TP Link has failed */
	    lp->irq_mask &= ~IMR_LFM;
	}

	if (sts & STS_UNF) {             /* Transmit underrun */
	    de4x5_txur(dev);
	}

	if (sts & STS_SE) {              /* Bus Error */
	    STOP_DE4X5;
	    printk("%s: Fatal bus error occurred, sts=%#8x, device stopped.\n",
		   dev->name, sts);
	    spin_unlock(&lp->lock);
	    return IRQ_HANDLED;
	}
    }

    /* Load the TX ring with any locally stored packets */
    if (!test_and_set_bit(0, (void *)&lp->cache.lock)) {
	while (!skb_queue_empty(&lp->cache.queue) && !netif_queue_stopped(dev) && lp->tx_enable) {
	    de4x5_queue_pkt(de4x5_get_cache(dev), dev);
	}
	lp->cache.lock = 0;
    }

    lp->interrupt = UNMASK_INTERRUPTS;
    ENABLE_IRQs;
    spin_unlock(&lp->lock);

    return IRQ_RETVAL(handled);
}

static int
de4x5_rx(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;
    int entry;
    s32 status;

    for (entry=lp->rx_new; (s32)le32_to_cpu(lp->rx_ring[entry].status)>=0;
	                                                    entry=lp->rx_new) {
	status = (s32)le32_to_cpu(lp->rx_ring[entry].status);

	if (lp->rx_ovf) {
	    if (inl(DE4X5_MFC) & MFC_FOCM) {
		de4x5_rx_ovfc(dev);
		break;
	    }
	}

	if (status & RD_FS) {                 /* Remember the start of frame */
	    lp->rx_old = entry;
	}

	if (status & RD_LS) {                 /* Valid frame status */
	    if (lp->tx_enable) lp->linkOK++;
	    if (status & RD_ES) {	      /* There was an error. */
		lp->stats.rx_errors++;        /* Update the error stats. */
		if (status & (RD_RF | RD_TL)) lp->stats.rx_frame_errors++;
		if (status & RD_CE)           lp->stats.rx_crc_errors++;
		if (status & RD_OF)           lp->stats.rx_fifo_errors++;
		if (status & RD_TL)           lp->stats.rx_length_errors++;
		if (status & RD_RF)           lp->pktStats.rx_runt_frames++;
		if (status & RD_CS)           lp->pktStats.rx_collision++;
		if (status & RD_DB)           lp->pktStats.rx_dribble++;
		if (status & RD_OF)           lp->pktStats.rx_overflow++;
	    } else {                          /* A valid frame received */
		struct sk_buff *skb;
		short pkt_len = (short)(le32_to_cpu(lp->rx_ring[entry].status)
					                            >> 16) - 4;

		if ((skb = de4x5_alloc_rx_buff(dev, entry, pkt_len)) == NULL) {
		    printk("%s: Insufficient memory; nuking packet.\n",
			                                            dev->name);
		    lp->stats.rx_dropped++;
		} else {
		    de4x5_dbg_rx(skb, pkt_len);

		    /* Push up the protocol stack */
		    skb->protocol=eth_type_trans(skb,dev);
		    de4x5_local_stats(dev, skb->data, pkt_len);
		    netif_rx(skb);

		    /* Update stats */
		    lp->stats.rx_packets++;
 		    lp->stats.rx_bytes += pkt_len;
		}
	    }

	    /* Change buffer ownership for this frame, back to the adapter */
	    for (;lp->rx_old!=entry;lp->rx_old=(lp->rx_old + 1)%lp->rxRingSize) {
		lp->rx_ring[lp->rx_old].status = cpu_to_le32(R_OWN);
		barrier();
	    }
	    lp->rx_ring[entry].status = cpu_to_le32(R_OWN);
	    barrier();
	}

	/*
	** Update entry information
	*/
	lp->rx_new = (lp->rx_new + 1) % lp->rxRingSize;
    }

    return 0;
}

static inline void
de4x5_free_tx_buff(struct de4x5_private *lp, int entry)
{
    dma_unmap_single(lp->gendev, le32_to_cpu(lp->tx_ring[entry].buf),
		     le32_to_cpu(lp->tx_ring[entry].des1) & TD_TBS1,
		     DMA_TO_DEVICE);
    if ((u_long) lp->tx_skb[entry] > 1)
	dev_kfree_skb_irq(lp->tx_skb[entry]);
    lp->tx_skb[entry] = NULL;
}

/*
** Buffer sent - check for TX buffer errors.
*/
static int
de4x5_tx(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;
    int entry;
    s32 status;

    for (entry = lp->tx_old; entry != lp->tx_new; entry = lp->tx_old) {
	status = (s32)le32_to_cpu(lp->tx_ring[entry].status);
	if (status < 0) {                     /* Buffer not sent yet */
	    break;
	} else if (status != 0x7fffffff) {    /* Not setup frame */
	    if (status & TD_ES) {             /* An error happened */
		lp->stats.tx_errors++;
		if (status & TD_NC) lp->stats.tx_carrier_errors++;
		if (status & TD_LC) lp->stats.tx_window_errors++;
		if (status & TD_UF) lp->stats.tx_fifo_errors++;
		if (status & TD_EC) lp->pktStats.excessive_collisions++;
		if (status & TD_DE) lp->stats.tx_aborted_errors++;

		if (TX_PKT_PENDING) {
		    outl(POLL_DEMAND, DE4X5_TPD);/* Restart a stalled TX */
		}
	    } else {                      /* Packet sent */
		lp->stats.tx_packets++;
		if (lp->tx_enable) lp->linkOK++;
	    }
	    /* Update the collision counter */
	    lp->stats.collisions += ((status & TD_EC) ? 16 :
				                      ((status & TD_CC) >> 3));

	    /* Free the buffer. */
	    if (lp->tx_skb[entry] != NULL)
	    	de4x5_free_tx_buff(lp, entry);
	}

	/* Update all the pointers */
	lp->tx_old = (lp->tx_old + 1) % lp->txRingSize;
    }

    /* Any resources available? */
    if (TX_BUFFS_AVAIL && netif_queue_stopped(dev)) {
	if (lp->interrupt)
	    netif_wake_queue(dev);
	else
	    netif_start_queue(dev);
    }

    return 0;
}

static void
de4x5_ast(struct net_device *dev)
{
	struct de4x5_private *lp = netdev_priv(dev);
	int next_tick = DE4X5_AUTOSENSE_MS;
	int dt;

	if (lp->useSROM)
		next_tick = srom_autoconf(dev);
	else if (lp->chipset == DC21140)
		next_tick = dc21140m_autoconf(dev);
	else if (lp->chipset == DC21041)
		next_tick = dc21041_autoconf(dev);
	else if (lp->chipset == DC21040)
		next_tick = dc21040_autoconf(dev);
	lp->linkOK = 0;

	dt = (next_tick * HZ) / 1000;

	if (!dt)
		dt = 1;

	mod_timer(&lp->timer, jiffies + dt);
}

static int
de4x5_txur(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;
    int omr;

    omr = inl(DE4X5_OMR);
    if (!(omr & OMR_SF) || (lp->chipset==DC21041) || (lp->chipset==DC21040)) {
	omr &= ~(OMR_ST|OMR_SR);
	outl(omr, DE4X5_OMR);
	while (inl(DE4X5_STS) & STS_TS);
	if ((omr & OMR_TR) < OMR_TR) {
	    omr += 0x4000;
	} else {
	    omr |= OMR_SF;
	}
	outl(omr | OMR_ST | OMR_SR, DE4X5_OMR);
    }

    return 0;
}

static int
de4x5_rx_ovfc(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;
    int omr;

    omr = inl(DE4X5_OMR);
    outl(omr & ~OMR_SR, DE4X5_OMR);
    while (inl(DE4X5_STS) & STS_RS);

    for (; (s32)le32_to_cpu(lp->rx_ring[lp->rx_new].status)>=0;) {
	lp->rx_ring[lp->rx_new].status = cpu_to_le32(R_OWN);
	lp->rx_new = (lp->rx_new + 1) % lp->rxRingSize;
    }

    outl(omr, DE4X5_OMR);

    return 0;
}

static int
de4x5_close(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;
    s32 imr, omr;

    disable_ast(dev);

    netif_stop_queue(dev);

    if (de4x5_debug & DEBUG_CLOSE) {
	printk("%s: Shutting down ethercard, status was %8.8x.\n",
	       dev->name, inl(DE4X5_STS));
    }

    /*
    ** We stop the DE4X5 here... mask interrupts and stop TX & RX
    */
    DISABLE_IRQs;
    STOP_DE4X5;

    /* Free the associated irq */
    free_irq(dev->irq, dev);
    lp->state = CLOSED;

    /* Free any socket buffers */
    de4x5_free_rx_buffs(dev);
    de4x5_free_tx_buffs(dev);

    /* Put the adapter to sleep to save power */
    yawn(dev, SLEEP);

    return 0;
}

static struct net_device_stats *
de4x5_get_stats(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;

    lp->stats.rx_missed_errors = (int)(inl(DE4X5_MFC) & (MFC_OVFL | MFC_CNTR));

    return &lp->stats;
}

static void
de4x5_local_stats(struct net_device *dev, char *buf, int pkt_len)
{
    struct de4x5_private *lp = netdev_priv(dev);
    int i;

    for (i=1; i<DE4X5_PKT_STAT_SZ-1; i++) {
        if (pkt_len < (i*DE4X5_PKT_BIN_SZ)) {
	    lp->pktStats.bins[i]++;
	    i = DE4X5_PKT_STAT_SZ;
	}
    }
    if (is_multicast_ether_addr(buf)) {
        if (is_broadcast_ether_addr(buf)) {
	    lp->pktStats.broadcast++;
	} else {
	    lp->pktStats.multicast++;
	}
    } else if (ether_addr_equal(buf, dev->dev_addr)) {
        lp->pktStats.unicast++;
    }

    lp->pktStats.bins[0]++;       /* Duplicates stats.rx_packets */
    if (lp->pktStats.bins[0] == 0) { /* Reset counters */
        memset((char *)&lp->pktStats, 0, sizeof(lp->pktStats));
    }
}

/*
** Removes the TD_IC flag from previous descriptor to improve TX performance.
** If the flag is changed on a descriptor that is being read by the hardware,
** I assume PCI transaction ordering will mean you are either successful or
** just miss asserting the change to the hardware. Anyway you're messing with
** a descriptor you don't own, but this shouldn't kill the chip provided
** the descriptor register is read only to the hardware.
*/
static void
load_packet(struct net_device *dev, char *buf, u32 flags, struct sk_buff *skb)
{
    struct de4x5_private *lp = netdev_priv(dev);
    int entry = (lp->tx_new ? lp->tx_new-1 : lp->txRingSize-1);
    dma_addr_t buf_dma = dma_map_single(lp->gendev, buf, flags & TD_TBS1, DMA_TO_DEVICE);

    lp->tx_ring[lp->tx_new].buf = cpu_to_le32(buf_dma);
    lp->tx_ring[lp->tx_new].des1 &= cpu_to_le32(TD_TER);
    lp->tx_ring[lp->tx_new].des1 |= cpu_to_le32(flags);
    lp->tx_skb[lp->tx_new] = skb;
    lp->tx_ring[entry].des1 &= cpu_to_le32(~TD_IC);
    barrier();

    lp->tx_ring[lp->tx_new].status = cpu_to_le32(T_OWN);
    barrier();
}

/*
** Set or clear the multicast filter for this adaptor.
*/
static void
set_multicast_list(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;

    /* First, double check that the adapter is open */
    if (lp->state == OPEN) {
	if (dev->flags & IFF_PROMISC) {         /* set promiscuous mode */
	    u32 omr;
	    omr = inl(DE4X5_OMR);
	    omr |= OMR_PR;
	    outl(omr, DE4X5_OMR);
	} else {
	    SetMulticastFilter(dev);
	    load_packet(dev, lp->setup_frame, TD_IC | PERFECT_F | TD_SET |
			                                SETUP_FRAME_LEN, (struct sk_buff *)1);

	    lp->tx_new = (lp->tx_new + 1) % lp->txRingSize;
	    outl(POLL_DEMAND, DE4X5_TPD);       /* Start the TX */
	    netif_trans_update(dev); /* prevent tx timeout */
	}
    }
}

/*
** Calculate the hash code and update the logical address filter
** from a list of ethernet multicast addresses.
** Little endian crc one liner from Matt Thomas, DEC.
*/
static void
SetMulticastFilter(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    struct netdev_hw_addr *ha;
    u_long iobase = dev->base_addr;
    int i, bit, byte;
    u16 hashcode;
    u32 omr, crc;
    char *pa;
    unsigned char *addrs;

    omr = inl(DE4X5_OMR);
    omr &= ~(OMR_PR | OMR_PM);
    pa = build_setup_frame(dev, ALL);        /* Build the basic frame */

    if ((dev->flags & IFF_ALLMULTI) || (netdev_mc_count(dev) > 14)) {
	omr |= OMR_PM;                       /* Pass all multicasts */
    } else if (lp->setup_f == HASH_PERF) {   /* Hash Filtering */
	netdev_for_each_mc_addr(ha, dev) {
		crc = ether_crc_le(ETH_ALEN, ha->addr);
		hashcode = crc & DE4X5_HASH_BITS;  /* hashcode is 9 LSb of CRC */

		byte = hashcode >> 3;        /* bit[3-8] -> byte in filter */
		bit = 1 << (hashcode & 0x07);/* bit[0-2] -> bit in byte */

		byte <<= 1;                  /* calc offset into setup frame */
		if (byte & 0x02) {
		    byte -= 1;
		}
		lp->setup_frame[byte] |= bit;
	}
    } else {                                 /* Perfect filtering */
	netdev_for_each_mc_addr(ha, dev) {
	    addrs = ha->addr;
	    for (i=0; i<ETH_ALEN; i++) {
		*(pa + (i&1)) = *addrs++;
		if (i & 0x01) pa += 4;
	    }
	}
    }
    outl(omr, DE4X5_OMR);
}

#ifdef CONFIG_EISA

static u_char de4x5_irq[] = EISA_ALLOWED_IRQ_LIST;

static int de4x5_eisa_probe(struct device *gendev)
{
	struct eisa_device *edev;
	u_long iobase;
	u_char irq, regval;
	u_short vendor;
	u32 cfid;
	int status, device;
	struct net_device *dev;
	struct de4x5_private *lp;

	edev = to_eisa_device (gendev);
	iobase = edev->base_addr;

	if (!request_region (iobase, DE4X5_EISA_TOTAL_SIZE, "de4x5"))
		return -EBUSY;

	if (!request_region (iobase + DE4X5_EISA_IO_PORTS,
			     DE4X5_EISA_TOTAL_SIZE, "de4x5")) {
		status = -EBUSY;
		goto release_reg_1;
	}

	if (!(dev = alloc_etherdev (sizeof (struct de4x5_private)))) {
		status = -ENOMEM;
		goto release_reg_2;
	}
	lp = netdev_priv(dev);

	cfid = (u32) inl(PCI_CFID);
	lp->cfrv = (u_short) inl(PCI_CFRV);
	device = (cfid >> 8) & 0x00ffff00;
	vendor = (u_short) cfid;

	/* Read the EISA Configuration Registers */
	regval = inb(EISA_REG0) & (ER0_INTL | ER0_INTT);
#ifdef CONFIG_ALPHA
	/* Looks like the Jensen firmware (rev 2.2) doesn't really
	 * care about the EISA configuration, and thus doesn't
	 * configure the PLX bridge properly. Oh well... Simply mimic
	 * the EISA config file to sort it out. */

	/* EISA REG1: Assert DecChip 21040 HW Reset */
	outb (ER1_IAM | 1, EISA_REG1);
	mdelay (1);

        /* EISA REG1: Deassert DecChip 21040 HW Reset */
	outb (ER1_IAM, EISA_REG1);
	mdelay (1);

	/* EISA REG3: R/W Burst Transfer Enable */
	outb (ER3_BWE | ER3_BRE, EISA_REG3);

	/* 32_bit slave/master, Preempt Time=23 bclks, Unlatched Interrupt */
	outb (ER0_BSW | ER0_BMW | ER0_EPT | regval, EISA_REG0);
#endif
	irq = de4x5_irq[(regval >> 1) & 0x03];

	if (is_DC2114x) {
	    device = ((lp->cfrv & CFRV_RN) < DC2114x_BRK ? DC21142 : DC21143);
	}
	lp->chipset = device;
	lp->bus = EISA;

	/* Write the PCI Configuration Registers */
	outl(PCI_COMMAND_IO | PCI_COMMAND_MASTER, PCI_CFCS);
	outl(0x00006000, PCI_CFLT);
	outl(iobase, PCI_CBIO);

	DevicePresent(dev, EISA_APROM);

	dev->irq = irq;

	if (!(status = de4x5_hw_init (dev, iobase, gendev))) {
		return 0;
	}

	free_netdev (dev);
 release_reg_2:
	release_region (iobase + DE4X5_EISA_IO_PORTS, DE4X5_EISA_TOTAL_SIZE);
 release_reg_1:
	release_region (iobase, DE4X5_EISA_TOTAL_SIZE);

	return status;
}

static int de4x5_eisa_remove(struct device *device)
{
	struct net_device *dev;
	u_long iobase;

	dev = dev_get_drvdata(device);
	iobase = dev->base_addr;

	unregister_netdev (dev);
	free_netdev (dev);
	release_region (iobase + DE4X5_EISA_IO_PORTS, DE4X5_EISA_TOTAL_SIZE);
	release_region (iobase, DE4X5_EISA_TOTAL_SIZE);

	return 0;
}

static struct eisa_device_id de4x5_eisa_ids[] = {
        { "DEC4250", 0 },	/* 0 is the board name index... */
        { "" }
};
MODULE_DEVICE_TABLE(eisa, de4x5_eisa_ids);

static struct eisa_driver de4x5_eisa_driver = {
        .id_table = de4x5_eisa_ids,
        .driver   = {
                .name    = "de4x5",
                .probe   = de4x5_eisa_probe,
		.remove  = de4x5_eisa_remove,
        }
};
MODULE_DEVICE_TABLE(eisa, de4x5_eisa_ids);
#endif

#ifdef CONFIG_PCI

/*
** This function searches the current bus (which is >0) for a DECchip with an
** SROM, so that in multiport cards that have one SROM shared between multiple
** DECchips, we can find the base SROM irrespective of the BIOS scan direction.
** For single port cards this is a time waster...
*/
static void
srom_search(struct net_device *dev, struct pci_dev *pdev)
{
    u_char pb;
    u_short vendor, status;
    u_int irq = 0, device;
    u_long iobase = 0;                     /* Clear upper 32 bits in Alphas */
    int i, j;
    struct de4x5_private *lp = netdev_priv(dev);
    struct pci_dev *this_dev;

    list_for_each_entry(this_dev, &pdev->bus->devices, bus_list) {
	vendor = this_dev->vendor;
	device = this_dev->device << 8;
	if (!(is_DC21040 || is_DC21041 || is_DC21140 || is_DC2114x)) continue;

	/* Get the chip configuration revision register */
	pb = this_dev->bus->number;

	/* Set the device number information */
	lp->device = PCI_SLOT(this_dev->devfn);
	lp->bus_num = pb;

	/* Set the chipset information */
	if (is_DC2114x) {
	    device = ((this_dev->revision & CFRV_RN) < DC2114x_BRK
		      ? DC21142 : DC21143);
	}
	lp->chipset = device;

	/* Get the board I/O address (64 bits on sparc64) */
	iobase = pci_resource_start(this_dev, 0);

	/* Fetch the IRQ to be used */
	irq = this_dev->irq;
	if ((irq == 0) || (irq == 0xff) || ((int)irq == -1)) continue;

	/* Check if I/O accesses are enabled */
	pci_read_config_word(this_dev, PCI_COMMAND, &status);
	if (!(status & PCI_COMMAND_IO)) continue;

	/* Search for a valid SROM attached to this DECchip */
	DevicePresent(dev, DE4X5_APROM);
	for (j=0, i=0; i<ETH_ALEN; i++) {
	    j += (u_char) *((u_char *)&lp->srom + SROM_HWADD + i);
	}
	if (j != 0 && j != 6 * 0xff) {
	    last.chipset = device;
	    last.bus = pb;
	    last.irq = irq;
	    for (i=0; i<ETH_ALEN; i++) {
		last.addr[i] = (u_char)*((u_char *)&lp->srom + SROM_HWADD + i);
	    }
	    return;
	}
    }
}

/*
** PCI bus I/O device probe
** NB: PCI I/O accesses and Bus Mastering are enabled by the PCI BIOS, not
** the driver. Some PCI BIOS's, pre V2.1, need the slot + features to be
** enabled by the user first in the set up utility. Hence we just check for
** enabled features and silently ignore the card if they're not.
**
** STOP PRESS: Some BIOS's __require__ the driver to enable the bus mastering
** bit. Here, check for I/O accesses and then set BM. If you put the card in
** a non BM slot, you're on your own (and complain to the PC vendor that your
** PC doesn't conform to the PCI standard)!
**
** This function is only compatible with the *latest* 2.1.x kernels. For 2.0.x
** kernels use the V0.535[n] drivers.
*/

static int de4x5_pci_probe(struct pci_dev *pdev,
			   const struct pci_device_id *ent)
{
	u_char pb, pbus = 0, dev_num, dnum = 0, timer;
	u_short vendor, status;
	u_int irq = 0, device;
	u_long iobase = 0;	/* Clear upper 32 bits in Alphas */
	int error;
	struct net_device *dev;
	struct de4x5_private *lp;

	dev_num = PCI_SLOT(pdev->devfn);
	pb = pdev->bus->number;

	if (io) { /* probe a single PCI device */
		pbus = (u_short)(io >> 8);
		dnum = (u_short)(io & 0xff);
		if ((pbus != pb) || (dnum != dev_num))
			return -ENODEV;
	}

	vendor = pdev->vendor;
	device = pdev->device << 8;
	if (!(is_DC21040 || is_DC21041 || is_DC21140 || is_DC2114x))
		return -ENODEV;

	/* Ok, the device seems to be for us. */
	if ((error = pci_enable_device (pdev)))
		return error;

	if (!(dev = alloc_etherdev (sizeof (struct de4x5_private)))) {
		error = -ENOMEM;
		goto disable_dev;
	}

	lp = netdev_priv(dev);
	lp->bus = PCI;
	lp->bus_num = 0;

	/* Search for an SROM on this bus */
	if (lp->bus_num != pb) {
	    lp->bus_num = pb;
	    srom_search(dev, pdev);
	}

	/* Get the chip configuration revision register */
	lp->cfrv = pdev->revision;

	/* Set the device number information */
	lp->device = dev_num;
	lp->bus_num = pb;

	/* Set the chipset information */
	if (is_DC2114x) {
	    device = ((lp->cfrv & CFRV_RN) < DC2114x_BRK ? DC21142 : DC21143);
	}
	lp->chipset = device;

	/* Get the board I/O address (64 bits on sparc64) */
	iobase = pci_resource_start(pdev, 0);

	/* Fetch the IRQ to be used */
	irq = pdev->irq;
	if ((irq == 0) || (irq == 0xff) || ((int)irq == -1)) {
		error = -ENODEV;
		goto free_dev;
	}

	/* Check if I/O accesses and Bus Mastering are enabled */
	pci_read_config_word(pdev, PCI_COMMAND, &status);
#ifdef __powerpc__
	if (!(status & PCI_COMMAND_IO)) {
	    status |= PCI_COMMAND_IO;
	    pci_write_config_word(pdev, PCI_COMMAND, status);
	    pci_read_config_word(pdev, PCI_COMMAND, &status);
	}
#endif /* __powerpc__ */
	if (!(status & PCI_COMMAND_IO)) {
		error = -ENODEV;
		goto free_dev;
	}

	if (!(status & PCI_COMMAND_MASTER)) {
	    status |= PCI_COMMAND_MASTER;
	    pci_write_config_word(pdev, PCI_COMMAND, status);
	    pci_read_config_word(pdev, PCI_COMMAND, &status);
	}
	if (!(status & PCI_COMMAND_MASTER)) {
		error = -ENODEV;
		goto free_dev;
	}

	/* Check the latency timer for values >= 0x60 */
	pci_read_config_byte(pdev, PCI_LATENCY_TIMER, &timer);
	if (timer < 0x60) {
	    pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0x60);
	}

	DevicePresent(dev, DE4X5_APROM);

	if (!request_region (iobase, DE4X5_PCI_TOTAL_SIZE, "de4x5")) {
		error = -EBUSY;
		goto free_dev;
	}

	dev->irq = irq;

	if ((error = de4x5_hw_init(dev, iobase, &pdev->dev))) {
		goto release;
	}

	return 0;

 release:
	release_region (iobase, DE4X5_PCI_TOTAL_SIZE);
 free_dev:
	free_netdev (dev);
 disable_dev:
	pci_disable_device (pdev);
	return error;
}

static void de4x5_pci_remove(struct pci_dev *pdev)
{
	struct net_device *dev;
	u_long iobase;

	dev = pci_get_drvdata(pdev);
	iobase = dev->base_addr;

	unregister_netdev (dev);
	free_netdev (dev);
	release_region (iobase, DE4X5_PCI_TOTAL_SIZE);
	pci_disable_device (pdev);
}

static const struct pci_device_id de4x5_pci_tbl[] = {
        { PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_DEC_TULIP,
          PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
        { PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_DEC_TULIP_PLUS,
          PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1 },
        { PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_DEC_TULIP_FAST,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 2 },
        { PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_DEC_21142,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 3 },
        { },
};

static struct pci_driver de4x5_pci_driver = {
        .name           = "de4x5",
        .id_table       = de4x5_pci_tbl,
        .probe          = de4x5_pci_probe,
	.remove         = de4x5_pci_remove,
};

#endif

/*
** Auto configure the media here rather than setting the port at compile
** time. This routine is called by de4x5_init() and when a loss of media is
** detected (excessive collisions, loss of carrier, no carrier or link fail
** [TP] or no recent receive activity) to check whether the user has been
** sneaky and changed the port on us.
*/
static int
autoconf_media(struct net_device *dev)
{
	struct de4x5_private *lp = netdev_priv(dev);
	u_long iobase = dev->base_addr;

	disable_ast(dev);

	lp->c_media = AUTO;                     /* Bogus last media */
	inl(DE4X5_MFC);                         /* Zero the lost frames counter */
	lp->media = INIT;
	lp->tcount = 0;

	de4x5_ast(dev);

	return lp->media;
}

/*
** Autoconfigure the media when using the DC21040. AUI cannot be distinguished
** from BNC as the port has a jumper to set thick or thin wire. When set for
** BNC, the BNC port will indicate activity if it's not terminated correctly.
** The only way to test for that is to place a loopback packet onto the
** network and watch for errors. Since we're messing with the interrupt mask
** register, disable the board interrupts and do not allow any more packets to
** be queued to the hardware. Re-enable everything only when the media is
** found.
** I may have to "age out" locally queued packets so that the higher layer
** timeouts don't effectively duplicate packets on the network.
*/
static int
dc21040_autoconf(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;
    int next_tick = DE4X5_AUTOSENSE_MS;
    s32 imr;

    switch (lp->media) {
    case INIT:
	DISABLE_IRQs;
	lp->tx_enable = false;
	lp->timeout = -1;
	de4x5_save_skbs(dev);
	if ((lp->autosense == AUTO) || (lp->autosense == TP)) {
	    lp->media = TP;
	} else if ((lp->autosense == BNC) || (lp->autosense == AUI) || (lp->autosense == BNC_AUI)) {
	    lp->media = BNC_AUI;
	} else if (lp->autosense == EXT_SIA) {
	    lp->media = EXT_SIA;
	} else {
	    lp->media = NC;
	}
	lp->local_state = 0;
	next_tick = dc21040_autoconf(dev);
	break;

    case TP:
	next_tick = dc21040_state(dev, 0x8f01, 0xffff, 0x0000, 3000, BNC_AUI,
		                                         TP_SUSPECT, test_tp);
	break;

    case TP_SUSPECT:
	next_tick = de4x5_suspect_state(dev, 1000, TP, test_tp, dc21040_autoconf);
	break;

    case BNC:
    case AUI:
    case BNC_AUI:
	next_tick = dc21040_state(dev, 0x8f09, 0x0705, 0x0006, 3000, EXT_SIA,
		                                  BNC_AUI_SUSPECT, ping_media);
	break;

    case BNC_AUI_SUSPECT:
	next_tick = de4x5_suspect_state(dev, 1000, BNC_AUI, ping_media, dc21040_autoconf);
	break;

    case EXT_SIA:
	next_tick = dc21040_state(dev, 0x3041, 0x0000, 0x0006, 3000,
		                              NC, EXT_SIA_SUSPECT, ping_media);
	break;

    case EXT_SIA_SUSPECT:
	next_tick = de4x5_suspect_state(dev, 1000, EXT_SIA, ping_media, dc21040_autoconf);
	break;

    case NC:
	/* default to TP for all */
	reset_init_sia(dev, 0x8f01, 0xffff, 0x0000);
	if (lp->media != lp->c_media) {
	    de4x5_dbg_media(dev);
	    lp->c_media = lp->media;
	}
	lp->media = INIT;
	lp->tx_enable = false;
	break;
    }

    return next_tick;
}

static int
dc21040_state(struct net_device *dev, int csr13, int csr14, int csr15, int timeout,
	      int next_state, int suspect_state,
	      int (*fn)(struct net_device *, int))
{
    struct de4x5_private *lp = netdev_priv(dev);
    int next_tick = DE4X5_AUTOSENSE_MS;
    int linkBad;

    switch (lp->local_state) {
    case 0:
	reset_init_sia(dev, csr13, csr14, csr15);
	lp->local_state++;
	next_tick = 500;
	break;

    case 1:
	if (!lp->tx_enable) {
	    linkBad = fn(dev, timeout);
	    if (linkBad < 0) {
		next_tick = linkBad & ~TIMER_CB;
	    } else {
		if (linkBad && (lp->autosense == AUTO)) {
		    lp->local_state = 0;
		    lp->media = next_state;
		} else {
		    de4x5_init_connection(dev);
		}
	    }
	} else if (!lp->linkOK && (lp->autosense == AUTO)) {
	    lp->media = suspect_state;
	    next_tick = 3000;
	}
	break;
    }

    return next_tick;
}

static int
de4x5_suspect_state(struct net_device *dev, int timeout, int prev_state,
		      int (*fn)(struct net_device *, int),
		      int (*asfn)(struct net_device *))
{
    struct de4x5_private *lp = netdev_priv(dev);
    int next_tick = DE4X5_AUTOSENSE_MS;
    int linkBad;

    switch (lp->local_state) {
    case 1:
	if (lp->linkOK) {
	    lp->media = prev_state;
	} else {
	    lp->local_state++;
	    next_tick = asfn(dev);
	}
	break;

    case 2:
	linkBad = fn(dev, timeout);
	if (linkBad < 0) {
	    next_tick = linkBad & ~TIMER_CB;
	} else if (!linkBad) {
	    lp->local_state--;
	    lp->media = prev_state;
	} else {
	    lp->media = INIT;
	    lp->tcount++;
	}
    }

    return next_tick;
}

/*
** Autoconfigure the media when using the DC21041. AUI needs to be tested
** before BNC, because the BNC port will indicate activity if it's not
** terminated correctly. The only way to test for that is to place a loopback
** packet onto the network and watch for errors. Since we're messing with
** the interrupt mask register, disable the board interrupts and do not allow
** any more packets to be queued to the hardware. Re-enable everything only
** when the media is found.
*/
static int
dc21041_autoconf(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;
    s32 sts, irqs, irq_mask, imr, omr;
    int next_tick = DE4X5_AUTOSENSE_MS;

    switch (lp->media) {
    case INIT:
	DISABLE_IRQs;
	lp->tx_enable = false;
	lp->timeout = -1;
	de4x5_save_skbs(dev);          /* Save non transmitted skb's */
	if ((lp->autosense == AUTO) || (lp->autosense == TP_NW)) {
	    lp->media = TP;            /* On chip auto negotiation is broken */
	} else if (lp->autosense == TP) {
	    lp->media = TP;
	} else if (lp->autosense == BNC) {
	    lp->media = BNC;
	} else if (lp->autosense == AUI) {
	    lp->media = AUI;
	} else {
	    lp->media = NC;
	}
	lp->local_state = 0;
	next_tick = dc21041_autoconf(dev);
	break;

    case TP_NW:
	if (lp->timeout < 0) {
	    omr = inl(DE4X5_OMR);/* Set up full duplex for the autonegotiate */
	    outl(omr | OMR_FDX, DE4X5_OMR);
	}
	irqs = STS_LNF | STS_LNP;
	irq_mask = IMR_LFM | IMR_LPM;
	sts = test_media(dev, irqs, irq_mask, 0xef01, 0xffff, 0x0008, 2400);
	if (sts < 0) {
	    next_tick = sts & ~TIMER_CB;
	} else {
	    if (sts & STS_LNP) {
		lp->media = ANS;
	    } else {
		lp->media = AUI;
	    }
	    next_tick = dc21041_autoconf(dev);
	}
	break;

    case ANS:
	if (!lp->tx_enable) {
	    irqs = STS_LNP;
	    irq_mask = IMR_LPM;
	    sts = test_ans(dev, irqs, irq_mask, 3000);
	    if (sts < 0) {
		next_tick = sts & ~TIMER_CB;
	    } else {
		if (!(sts & STS_LNP) && (lp->autosense == AUTO)) {
		    lp->media = TP;
		    next_tick = dc21041_autoconf(dev);
		} else {
		    lp->local_state = 1;
		    de4x5_init_connection(dev);
		}
	    }
	} else if (!lp->linkOK && (lp->autosense == AUTO)) {
	    lp->media = ANS_SUSPECT;
	    next_tick = 3000;
	}
	break;

    case ANS_SUSPECT:
	next_tick = de4x5_suspect_state(dev, 1000, ANS, test_tp, dc21041_autoconf);
	break;

    case TP:
	if (!lp->tx_enable) {
	    if (lp->timeout < 0) {
		omr = inl(DE4X5_OMR);          /* Set up half duplex for TP */
		outl(omr & ~OMR_FDX, DE4X5_OMR);
	    }
	    irqs = STS_LNF | STS_LNP;
	    irq_mask = IMR_LFM | IMR_LPM;
	    sts = test_media(dev,irqs, irq_mask, 0xef01, 0xff3f, 0x0008, 2400);
	    if (sts < 0) {
		next_tick = sts & ~TIMER_CB;
	    } else {
		if (!(sts & STS_LNP) && (lp->autosense == AUTO)) {
		    if (inl(DE4X5_SISR) & SISR_NRA) {
			lp->media = AUI;       /* Non selected port activity */
		    } else {
			lp->media = BNC;
		    }
		    next_tick = dc21041_autoconf(dev);
		} else {
		    lp->local_state = 1;
		    de4x5_init_connection(dev);
		}
	    }
	} else if (!lp->linkOK && (lp->autosense == AUTO)) {
	    lp->media = TP_SUSPECT;
	    next_tick = 3000;
	}
	break;

    case TP_SUSPECT:
	next_tick = de4x5_suspect_state(dev, 1000, TP, test_tp, dc21041_autoconf);
	break;

    case AUI:
	if (!lp->tx_enable) {
	    if (lp->timeout < 0) {
		omr = inl(DE4X5_OMR);          /* Set up half duplex for AUI */
		outl(omr & ~OMR_FDX, DE4X5_OMR);
	    }
	    irqs = 0;
	    irq_mask = 0;
	    sts = test_media(dev,irqs, irq_mask, 0xef09, 0xf73d, 0x000e, 1000);
	    if (sts < 0) {
		next_tick = sts & ~TIMER_CB;
	    } else {
		if (!(inl(DE4X5_SISR) & SISR_SRA) && (lp->autosense == AUTO)) {
		    lp->media = BNC;
		    next_tick = dc21041_autoconf(dev);
		} else {
		    lp->local_state = 1;
		    de4x5_init_connection(dev);
		}
	    }
	} else if (!lp->linkOK && (lp->autosense == AUTO)) {
	    lp->media = AUI_SUSPECT;
	    next_tick = 3000;
	}
	break;

    case AUI_SUSPECT:
	next_tick = de4x5_suspect_state(dev, 1000, AUI, ping_media, dc21041_autoconf);
	break;

    case BNC:
	switch (lp->local_state) {
	case 0:
	    if (lp->timeout < 0) {
		omr = inl(DE4X5_OMR);          /* Set up half duplex for BNC */
		outl(omr & ~OMR_FDX, DE4X5_OMR);
	    }
	    irqs = 0;
	    irq_mask = 0;
	    sts = test_media(dev,irqs, irq_mask, 0xef09, 0xf73d, 0x0006, 1000);
	    if (sts < 0) {
		next_tick = sts & ~TIMER_CB;
	    } else {
		lp->local_state++;             /* Ensure media connected */
		next_tick = dc21041_autoconf(dev);
	    }
	    break;

	case 1:
	    if (!lp->tx_enable) {
		if ((sts = ping_media(dev, 3000)) < 0) {
		    next_tick = sts & ~TIMER_CB;
		} else {
		    if (sts) {
			lp->local_state = 0;
			lp->media = NC;
		    } else {
			de4x5_init_connection(dev);
		    }
		}
	    } else if (!lp->linkOK && (lp->autosense == AUTO)) {
		lp->media = BNC_SUSPECT;
		next_tick = 3000;
	    }
	    break;
	}
	break;

    case BNC_SUSPECT:
	next_tick = de4x5_suspect_state(dev, 1000, BNC, ping_media, dc21041_autoconf);
	break;

    case NC:
	omr = inl(DE4X5_OMR);    /* Set up full duplex for the autonegotiate */
	outl(omr | OMR_FDX, DE4X5_OMR);
	reset_init_sia(dev, 0xef01, 0xffff, 0x0008);/* Initialise the SIA */
	if (lp->media != lp->c_media) {
	    de4x5_dbg_media(dev);
	    lp->c_media = lp->media;
	}
	lp->media = INIT;
	lp->tx_enable = false;
	break;
    }

    return next_tick;
}

/*
** Some autonegotiation chips are broken in that they do not return the
** acknowledge bit (anlpa & MII_ANLPA_ACK) in the link partner advertisement
** register, except at the first power up negotiation.
*/
static int
dc21140m_autoconf(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    int ana, anlpa, cap, cr, slnk, sr;
    int next_tick = DE4X5_AUTOSENSE_MS;
    u_long imr, omr, iobase = dev->base_addr;

    switch(lp->media) {
    case INIT:
        if (lp->timeout < 0) {
	    DISABLE_IRQs;
	    lp->tx_enable = false;
	    lp->linkOK = 0;
	    de4x5_save_skbs(dev);          /* Save non transmitted skb's */
	}
	if ((next_tick = de4x5_reset_phy(dev)) < 0) {
	    next_tick &= ~TIMER_CB;
	} else {
	    if (lp->useSROM) {
		if (srom_map_media(dev) < 0) {
		    lp->tcount++;
		    return next_tick;
		}
		srom_exec(dev, lp->phy[lp->active].gep);
		if (lp->infoblock_media == ANS) {
		    ana = lp->phy[lp->active].ana | MII_ANA_CSMA;
		    mii_wr(ana, MII_ANA, lp->phy[lp->active].addr, DE4X5_MII);
		}
	    } else {
		lp->tmp = MII_SR_ASSC;     /* Fake out the MII speed set */
		SET_10Mb;
		if (lp->autosense == _100Mb) {
		    lp->media = _100Mb;
		} else if (lp->autosense == _10Mb) {
		    lp->media = _10Mb;
		} else if ((lp->autosense == AUTO) &&
			            ((sr=is_anc_capable(dev)) & MII_SR_ANC)) {
		    ana = (((sr >> 6) & MII_ANA_TAF) | MII_ANA_CSMA);
		    ana &= (lp->fdx ? ~0 : ~MII_ANA_FDAM);
		    mii_wr(ana, MII_ANA, lp->phy[lp->active].addr, DE4X5_MII);
		    lp->media = ANS;
		} else if (lp->autosense == AUTO) {
		    lp->media = SPD_DET;
		} else if (is_spd_100(dev) && is_100_up(dev)) {
		    lp->media = _100Mb;
		} else {
		    lp->media = NC;
		}
	    }
	    lp->local_state = 0;
	    next_tick = dc21140m_autoconf(dev);
	}
	break;

    case ANS:
	switch (lp->local_state) {
	case 0:
	    if (lp->timeout < 0) {
		mii_wr(MII_CR_ASSE | MII_CR_RAN, MII_CR, lp->phy[lp->active].addr, DE4X5_MII);
	    }
	    cr = test_mii_reg(dev, MII_CR, MII_CR_RAN, false, 500);
	    if (cr < 0) {
		next_tick = cr & ~TIMER_CB;
	    } else {
		if (cr) {
		    lp->local_state = 0;
		    lp->media = SPD_DET;
		} else {
		    lp->local_state++;
		}
		next_tick = dc21140m_autoconf(dev);
	    }
	    break;

	case 1:
	    if ((sr=test_mii_reg(dev, MII_SR, MII_SR_ASSC, true, 2000)) < 0) {
		next_tick = sr & ~TIMER_CB;
	    } else {
		lp->media = SPD_DET;
		lp->local_state = 0;
		if (sr) {                         /* Success! */
		    lp->tmp = MII_SR_ASSC;
		    anlpa = mii_rd(MII_ANLPA, lp->phy[lp->active].addr, DE4X5_MII);
		    ana = mii_rd(MII_ANA, lp->phy[lp->active].addr, DE4X5_MII);
		    if (!(anlpa & MII_ANLPA_RF) &&
			 (cap = anlpa & MII_ANLPA_TAF & ana)) {
			if (cap & MII_ANA_100M) {
			    lp->fdx = (ana & anlpa & MII_ANA_FDAM & MII_ANA_100M) != 0;
			    lp->media = _100Mb;
			} else if (cap & MII_ANA_10M) {
			    lp->fdx = (ana & anlpa & MII_ANA_FDAM & MII_ANA_10M) != 0;

			    lp->media = _10Mb;
			}
		    }
		}                       /* Auto Negotiation failed to finish */
		next_tick = dc21140m_autoconf(dev);
	    }                           /* Auto Negotiation failed to start */
	    break;
	}
	break;

    case SPD_DET:                              /* Choose 10Mb/s or 100Mb/s */
        if (lp->timeout < 0) {
	    lp->tmp = (lp->phy[lp->active].id ? MII_SR_LKS :
		                                  (~gep_rd(dev) & GEP_LNP));
	    SET_100Mb_PDET;
	}
        if ((slnk = test_for_100Mb(dev, 6500)) < 0) {
	    next_tick = slnk & ~TIMER_CB;
	} else {
	    if (is_spd_100(dev) && is_100_up(dev)) {
		lp->media = _100Mb;
	    } else if ((!is_spd_100(dev) && (is_10_up(dev) & lp->tmp))) {
		lp->media = _10Mb;
	    } else {
		lp->media = NC;
	    }
	    next_tick = dc21140m_autoconf(dev);
	}
	break;

    case _100Mb:                               /* Set 100Mb/s */
        next_tick = 3000;
	if (!lp->tx_enable) {
	    SET_100Mb;
	    de4x5_init_connection(dev);
	} else {
	    if (!lp->linkOK && (lp->autosense == AUTO)) {
		if (!is_100_up(dev) || (!lp->useSROM && !is_spd_100(dev))) {
		    lp->media = INIT;
		    lp->tcount++;
		    next_tick = DE4X5_AUTOSENSE_MS;
		}
	    }
	}
	break;

    case BNC:
    case AUI:
    case _10Mb:                                /* Set 10Mb/s */
        next_tick = 3000;
	if (!lp->tx_enable) {
	    SET_10Mb;
	    de4x5_init_connection(dev);
	} else {
	    if (!lp->linkOK && (lp->autosense == AUTO)) {
		if (!is_10_up(dev) || (!lp->useSROM && is_spd_100(dev))) {
		    lp->media = INIT;
		    lp->tcount++;
		    next_tick = DE4X5_AUTOSENSE_MS;
		}
	    }
	}
	break;

    case NC:
        if (lp->media != lp->c_media) {
	    de4x5_dbg_media(dev);
	    lp->c_media = lp->media;
	}
	lp->media = INIT;
	lp->tx_enable = false;
	break;
    }

    return next_tick;
}

/*
** This routine may be merged into dc21140m_autoconf() sometime as I'm
** changing how I figure out the media - but trying to keep it backwards
** compatible with the de500-xa and de500-aa.
** Whether it's BNC, AUI, SYM or MII is sorted out in the infoblock
** functions and set during de4x5_mac_port() and/or de4x5_reset_phy().
** This routine just has to figure out whether 10Mb/s or 100Mb/s is
** active.
** When autonegotiation is working, the ANS part searches the SROM for
** the highest common speed (TP) link that both can run and if that can
** be full duplex. That infoblock is executed and then the link speed set.
**
** Only _10Mb and _100Mb are tested here.
*/
static int
dc2114x_autoconf(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;
    s32 cr, anlpa, ana, cap, irqs, irq_mask, imr, omr, slnk, sr, sts;
    int next_tick = DE4X5_AUTOSENSE_MS;

    switch (lp->media) {
    case INIT:
        if (lp->timeout < 0) {
	    DISABLE_IRQs;
	    lp->tx_enable = false;
	    lp->linkOK = 0;
            lp->timeout = -1;
	    de4x5_save_skbs(dev);            /* Save non transmitted skb's */
	    if (lp->params.autosense & ~AUTO) {
		srom_map_media(dev);         /* Fixed media requested      */
		if (lp->media != lp->params.autosense) {
		    lp->tcount++;
		    lp->media = INIT;
		    return next_tick;
		}
		lp->media = INIT;
	    }
	}
	if ((next_tick = de4x5_reset_phy(dev)) < 0) {
	    next_tick &= ~TIMER_CB;
	} else {
	    if (lp->autosense == _100Mb) {
		lp->media = _100Mb;
	    } else if (lp->autosense == _10Mb) {
		lp->media = _10Mb;
	    } else if (lp->autosense == TP) {
		lp->media = TP;
	    } else if (lp->autosense == BNC) {
		lp->media = BNC;
	    } else if (lp->autosense == AUI) {
		lp->media = AUI;
	    } else {
		lp->media = SPD_DET;
		if ((lp->infoblock_media == ANS) &&
		                    ((sr=is_anc_capable(dev)) & MII_SR_ANC)) {
		    ana = (((sr >> 6) & MII_ANA_TAF) | MII_ANA_CSMA);
		    ana &= (lp->fdx ? ~0 : ~MII_ANA_FDAM);
		    mii_wr(ana, MII_ANA, lp->phy[lp->active].addr, DE4X5_MII);
		    lp->media = ANS;
		}
	    }
	    lp->local_state = 0;
	    next_tick = dc2114x_autoconf(dev);
        }
	break;

    case ANS:
	switch (lp->local_state) {
	case 0:
	    if (lp->timeout < 0) {
		mii_wr(MII_CR_ASSE | MII_CR_RAN, MII_CR, lp->phy[lp->active].addr, DE4X5_MII);
	    }
	    cr = test_mii_reg(dev, MII_CR, MII_CR_RAN, false, 500);
	    if (cr < 0) {
		next_tick = cr & ~TIMER_CB;
	    } else {
		if (cr) {
		    lp->local_state = 0;
		    lp->media = SPD_DET;
		} else {
		    lp->local_state++;
		}
		next_tick = dc2114x_autoconf(dev);
	    }
	    break;

	case 1:
	    sr = test_mii_reg(dev, MII_SR, MII_SR_ASSC, true, 2000);
	    if (sr < 0) {
		next_tick = sr & ~TIMER_CB;
	    } else {
		lp->media = SPD_DET;
		lp->local_state = 0;
		if (sr) {                         /* Success! */
		    lp->tmp = MII_SR_ASSC;
		    anlpa = mii_rd(MII_ANLPA, lp->phy[lp->active].addr, DE4X5_MII);
		    ana = mii_rd(MII_ANA, lp->phy[lp->active].addr, DE4X5_MII);
		    if (!(anlpa & MII_ANLPA_RF) &&
			 (cap = anlpa & MII_ANLPA_TAF & ana)) {
			if (cap & MII_ANA_100M) {
			    lp->fdx = (ana & anlpa & MII_ANA_FDAM & MII_ANA_100M) != 0;
			    lp->media = _100Mb;
			} else if (cap & MII_ANA_10M) {
			    lp->fdx = (ana & anlpa & MII_ANA_FDAM & MII_ANA_10M) != 0;
			    lp->media = _10Mb;
			}
		    }
		}                       /* Auto Negotiation failed to finish */
		next_tick = dc2114x_autoconf(dev);
	    }                           /* Auto Negotiation failed to start  */
	    break;
	}
	break;

    case AUI:
	if (!lp->tx_enable) {
	    if (lp->timeout < 0) {
		omr = inl(DE4X5_OMR);   /* Set up half duplex for AUI        */
		outl(omr & ~OMR_FDX, DE4X5_OMR);
	    }
	    irqs = 0;
	    irq_mask = 0;
	    sts = test_media(dev,irqs, irq_mask, 0, 0, 0, 1000);
	    if (sts < 0) {
		next_tick = sts & ~TIMER_CB;
	    } else {
		if (!(inl(DE4X5_SISR) & SISR_SRA) && (lp->autosense == AUTO)) {
		    lp->media = BNC;
		    next_tick = dc2114x_autoconf(dev);
		} else {
		    lp->local_state = 1;
		    de4x5_init_connection(dev);
		}
	    }
	} else if (!lp->linkOK && (lp->autosense == AUTO)) {
	    lp->media = AUI_SUSPECT;
	    next_tick = 3000;
	}
	break;

    case AUI_SUSPECT:
	next_tick = de4x5_suspect_state(dev, 1000, AUI, ping_media, dc2114x_autoconf);
	break;

    case BNC:
	switch (lp->local_state) {
	case 0:
	    if (lp->timeout < 0) {
		omr = inl(DE4X5_OMR);          /* Set up half duplex for BNC */
		outl(omr & ~OMR_FDX, DE4X5_OMR);
	    }
	    irqs = 0;
	    irq_mask = 0;
	    sts = test_media(dev,irqs, irq_mask, 0, 0, 0, 1000);
	    if (sts < 0) {
		next_tick = sts & ~TIMER_CB;
	    } else {
		lp->local_state++;             /* Ensure media connected */
		next_tick = dc2114x_autoconf(dev);
	    }
	    break;

	case 1:
	    if (!lp->tx_enable) {
		if ((sts = ping_media(dev, 3000)) < 0) {
		    next_tick = sts & ~TIMER_CB;
		} else {
		    if (sts) {
			lp->local_state = 0;
			lp->tcount++;
			lp->media = INIT;
		    } else {
			de4x5_init_connection(dev);
		    }
		}
	    } else if (!lp->linkOK && (lp->autosense == AUTO)) {
		lp->media = BNC_SUSPECT;
		next_tick = 3000;
	    }
	    break;
	}
	break;

    case BNC_SUSPECT:
	next_tick = de4x5_suspect_state(dev, 1000, BNC, ping_media, dc2114x_autoconf);
	break;

    case SPD_DET:                              /* Choose 10Mb/s or 100Mb/s */
	  if (srom_map_media(dev) < 0) {
	      lp->tcount++;
	      lp->media = INIT;
	      return next_tick;
	  }
	  if (lp->media == _100Mb) {
	      if ((slnk = test_for_100Mb(dev, 6500)) < 0) {
		  lp->media = SPD_DET;
		  return slnk & ~TIMER_CB;
	      }
	  } else {
	      if (wait_for_link(dev) < 0) {
		  lp->media = SPD_DET;
		  return PDET_LINK_WAIT;
	      }
	  }
	  if (lp->media == ANS) {           /* Do MII parallel detection */
	      if (is_spd_100(dev)) {
		  lp->media = _100Mb;
	      } else {
		  lp->media = _10Mb;
	      }
	      next_tick = dc2114x_autoconf(dev);
	  } else if (((lp->media == _100Mb) && is_100_up(dev)) ||
		     (((lp->media == _10Mb) || (lp->media == TP) ||
		       (lp->media == BNC)   || (lp->media == AUI)) &&
		      is_10_up(dev))) {
	      next_tick = dc2114x_autoconf(dev);
	  } else {
	      lp->tcount++;
	      lp->media = INIT;
	  }
	  break;

    case _10Mb:
        next_tick = 3000;
	if (!lp->tx_enable) {
	    SET_10Mb;
	    de4x5_init_connection(dev);
	} else {
	    if (!lp->linkOK && (lp->autosense == AUTO)) {
		if (!is_10_up(dev) || (!lp->useSROM && is_spd_100(dev))) {
		    lp->media = INIT;
		    lp->tcount++;
		    next_tick = DE4X5_AUTOSENSE_MS;
		}
	    }
	}
	break;

    case _100Mb:
        next_tick = 3000;
	if (!lp->tx_enable) {
	    SET_100Mb;
	    de4x5_init_connection(dev);
	} else {
	    if (!lp->linkOK && (lp->autosense == AUTO)) {
		if (!is_100_up(dev) || (!lp->useSROM && !is_spd_100(dev))) {
		    lp->media = INIT;
		    lp->tcount++;
		    next_tick = DE4X5_AUTOSENSE_MS;
		}
	    }
	}
	break;

    default:
	lp->tcount++;
printk("Huh?: media:%02x\n", lp->media);
	lp->media = INIT;
	break;
    }

    return next_tick;
}

static int
srom_autoconf(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);

    return lp->infoleaf_fn(dev);
}

/*
** This mapping keeps the original media codes and FDX flag unchanged.
** While it isn't strictly necessary, it helps me for the moment...
** The early return avoids a media state / SROM media space clash.
*/
static int
srom_map_media(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);

    lp->fdx = false;
    if (lp->infoblock_media == lp->media)
      return 0;

    switch(lp->infoblock_media) {
      case SROM_10BASETF:
	if (!lp->params.fdx) return -1;
	lp->fdx = true;
      case SROM_10BASET:
	if (lp->params.fdx && !lp->fdx) return -1;
	if ((lp->chipset == DC21140) || ((lp->chipset & ~0x00ff) == DC2114x)) {
	    lp->media = _10Mb;
	} else {
	    lp->media = TP;
	}
	break;

      case SROM_10BASE2:
	lp->media = BNC;
	break;

      case SROM_10BASE5:
	lp->media = AUI;
	break;

      case SROM_100BASETF:
        if (!lp->params.fdx) return -1;
	lp->fdx = true;
      case SROM_100BASET:
	if (lp->params.fdx && !lp->fdx) return -1;
	lp->media = _100Mb;
	break;

      case SROM_100BASET4:
	lp->media = _100Mb;
	break;

      case SROM_100BASEFF:
	if (!lp->params.fdx) return -1;
	lp->fdx = true;
      case SROM_100BASEF:
	if (lp->params.fdx && !lp->fdx) return -1;
	lp->media = _100Mb;
	break;

      case ANS:
	lp->media = ANS;
	lp->fdx = lp->params.fdx;
	break;

      default:
	printk("%s: Bad media code [%d] detected in SROM!\n", dev->name,
	                                                  lp->infoblock_media);
	return -1;
    }

    return 0;
}

static void
de4x5_init_connection(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;
    u_long flags = 0;

    if (lp->media != lp->c_media) {
        de4x5_dbg_media(dev);
	lp->c_media = lp->media;          /* Stop scrolling media messages */
    }

    spin_lock_irqsave(&lp->lock, flags);
    de4x5_rst_desc_ring(dev);
    de4x5_setup_intr(dev);
    lp->tx_enable = true;
    spin_unlock_irqrestore(&lp->lock, flags);
    outl(POLL_DEMAND, DE4X5_TPD);

    netif_wake_queue(dev);
}

/*
** General PHY reset function. Some MII devices don't reset correctly
** since their MII address pins can float at voltages that are dependent
** on the signal pin use. Do a double reset to ensure a reset.
*/
static int
de4x5_reset_phy(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;
    int next_tick = 0;

    if ((lp->useSROM) || (lp->phy[lp->active].id)) {
	if (lp->timeout < 0) {
	    if (lp->useSROM) {
		if (lp->phy[lp->active].rst) {
		    srom_exec(dev, lp->phy[lp->active].rst);
		    srom_exec(dev, lp->phy[lp->active].rst);
		} else if (lp->rst) {          /* Type 5 infoblock reset */
		    srom_exec(dev, lp->rst);
		    srom_exec(dev, lp->rst);
		}
	    } else {
		PHY_HARD_RESET;
	    }
	    if (lp->useMII) {
	        mii_wr(MII_CR_RST, MII_CR, lp->phy[lp->active].addr, DE4X5_MII);
            }
        }
	if (lp->useMII) {
	    next_tick = test_mii_reg(dev, MII_CR, MII_CR_RST, false, 500);
	}
    } else if (lp->chipset == DC21140) {
	PHY_HARD_RESET;
    }

    return next_tick;
}

static int
test_media(struct net_device *dev, s32 irqs, s32 irq_mask, s32 csr13, s32 csr14, s32 csr15, s32 msec)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;
    s32 sts, csr12;

    if (lp->timeout < 0) {
	lp->timeout = msec/100;
	if (!lp->useSROM) {      /* Already done if by SROM, else dc2104[01] */
	    reset_init_sia(dev, csr13, csr14, csr15);
	}

	/* set up the interrupt mask */
	outl(irq_mask, DE4X5_IMR);

	/* clear all pending interrupts */
	sts = inl(DE4X5_STS);
	outl(sts, DE4X5_STS);

	/* clear csr12 NRA and SRA bits */
	if ((lp->chipset == DC21041) || lp->useSROM) {
	    csr12 = inl(DE4X5_SISR);
	    outl(csr12, DE4X5_SISR);
	}
    }

    sts = inl(DE4X5_STS) & ~TIMER_CB;

    if (!(sts & irqs) && --lp->timeout) {
	sts = 100 | TIMER_CB;
    } else {
	lp->timeout = -1;
    }

    return sts;
}

static int
test_tp(struct net_device *dev, s32 msec)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;
    int sisr;

    if (lp->timeout < 0) {
	lp->timeout = msec/100;
    }

    sisr = (inl(DE4X5_SISR) & ~TIMER_CB) & (SISR_LKF | SISR_NCR);

    if (sisr && --lp->timeout) {
	sisr = 100 | TIMER_CB;
    } else {
	lp->timeout = -1;
    }

    return sisr;
}

/*
** Samples the 100Mb Link State Signal. The sample interval is important
** because too fast a rate can give erroneous results and confuse the
** speed sense algorithm.
*/
#define SAMPLE_INTERVAL 500  /* ms */
#define SAMPLE_DELAY    2000 /* ms */
static int
test_for_100Mb(struct net_device *dev, int msec)
{
    struct de4x5_private *lp = netdev_priv(dev);
    int gep = 0, ret = ((lp->chipset & ~0x00ff)==DC2114x? -1 :GEP_SLNK);

    if (lp->timeout < 0) {
	if ((msec/SAMPLE_INTERVAL) <= 0) return 0;
	if (msec > SAMPLE_DELAY) {
	    lp->timeout = (msec - SAMPLE_DELAY)/SAMPLE_INTERVAL;
	    gep = SAMPLE_DELAY | TIMER_CB;
	    return gep;
	} else {
	    lp->timeout = msec/SAMPLE_INTERVAL;
	}
    }

    if (lp->phy[lp->active].id || lp->useSROM) {
	gep = is_100_up(dev) | is_spd_100(dev);
    } else {
	gep = (~gep_rd(dev) & (GEP_SLNK | GEP_LNP));
    }
    if (!(gep & ret) && --lp->timeout) {
	gep = SAMPLE_INTERVAL | TIMER_CB;
    } else {
	lp->timeout = -1;
    }

    return gep;
}

static int
wait_for_link(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);

    if (lp->timeout < 0) {
	lp->timeout = 1;
    }

    if (lp->timeout--) {
	return TIMER_CB;
    } else {
	lp->timeout = -1;
    }

    return 0;
}

/*
**
**
*/
static int
test_mii_reg(struct net_device *dev, int reg, int mask, bool pol, long msec)
{
    struct de4x5_private *lp = netdev_priv(dev);
    int test;
    u_long iobase = dev->base_addr;

    if (lp->timeout < 0) {
	lp->timeout = msec/100;
    }

    reg = mii_rd((u_char)reg, lp->phy[lp->active].addr, DE4X5_MII) & mask;
    test = (reg ^ (pol ? ~0 : 0)) & mask;

    if (test && --lp->timeout) {
	reg = 100 | TIMER_CB;
    } else {
	lp->timeout = -1;
    }

    return reg;
}

static int
is_spd_100(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;
    int spd;

    if (lp->useMII) {
	spd = mii_rd(lp->phy[lp->active].spd.reg, lp->phy[lp->active].addr, DE4X5_MII);
	spd = ~(spd ^ lp->phy[lp->active].spd.value);
	spd &= lp->phy[lp->active].spd.mask;
    } else if (!lp->useSROM) {                      /* de500-xa */
	spd = ((~gep_rd(dev)) & GEP_SLNK);
    } else {
	if ((lp->ibn == 2) || !lp->asBitValid)
	    return (lp->chipset == DC21143) ? (~inl(DE4X5_SISR)&SISR_LS100) : 0;

	spd = (lp->asBitValid & (lp->asPolarity ^ (gep_rd(dev) & lp->asBit))) |
	          (lp->linkOK & ~lp->asBitValid);
    }

    return spd;
}

static int
is_100_up(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;

    if (lp->useMII) {
	/* Double read for sticky bits & temporary drops */
	mii_rd(MII_SR, lp->phy[lp->active].addr, DE4X5_MII);
	return mii_rd(MII_SR, lp->phy[lp->active].addr, DE4X5_MII) & MII_SR_LKS;
    } else if (!lp->useSROM) {                       /* de500-xa */
	return (~gep_rd(dev)) & GEP_SLNK;
    } else {
	if ((lp->ibn == 2) || !lp->asBitValid)
	    return (lp->chipset == DC21143) ? (~inl(DE4X5_SISR)&SISR_LS100) : 0;

        return (lp->asBitValid&(lp->asPolarity^(gep_rd(dev)&lp->asBit))) |
		(lp->linkOK & ~lp->asBitValid);
    }
}

static int
is_10_up(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;

    if (lp->useMII) {
	/* Double read for sticky bits & temporary drops */
	mii_rd(MII_SR, lp->phy[lp->active].addr, DE4X5_MII);
	return mii_rd(MII_SR, lp->phy[lp->active].addr, DE4X5_MII) & MII_SR_LKS;
    } else if (!lp->useSROM) {                       /* de500-xa */
	return (~gep_rd(dev)) & GEP_LNP;
    } else {
	if ((lp->ibn == 2) || !lp->asBitValid)
	    return ((lp->chipset & ~0x00ff) == DC2114x) ?
		    (~inl(DE4X5_SISR)&SISR_LS10):
		    0;

	return	(lp->asBitValid&(lp->asPolarity^(gep_rd(dev)&lp->asBit))) |
		(lp->linkOK & ~lp->asBitValid);
    }
}

static int
is_anc_capable(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;

    if (lp->phy[lp->active].id && (!lp->useSROM || lp->useMII)) {
	return mii_rd(MII_SR, lp->phy[lp->active].addr, DE4X5_MII);
    } else if ((lp->chipset & ~0x00ff) == DC2114x) {
	return (inl(DE4X5_SISR) & SISR_LPN) >> 12;
    } else {
	return 0;
    }
}

/*
** Send a packet onto the media and watch for send errors that indicate the
** media is bad or unconnected.
*/
static int
ping_media(struct net_device *dev, int msec)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;
    int sisr;

    if (lp->timeout < 0) {
	lp->timeout = msec/100;

	lp->tmp = lp->tx_new;                /* Remember the ring position */
	load_packet(dev, lp->frame, TD_LS | TD_FS | sizeof(lp->frame), (struct sk_buff *)1);
	lp->tx_new = (lp->tx_new + 1) % lp->txRingSize;
	outl(POLL_DEMAND, DE4X5_TPD);
    }

    sisr = inl(DE4X5_SISR);

    if ((!(sisr & SISR_NCR)) &&
	((s32)le32_to_cpu(lp->tx_ring[lp->tmp].status) < 0) &&
	 (--lp->timeout)) {
	sisr = 100 | TIMER_CB;
    } else {
	if ((!(sisr & SISR_NCR)) &&
	    !(le32_to_cpu(lp->tx_ring[lp->tmp].status) & (T_OWN | TD_ES)) &&
	    lp->timeout) {
	    sisr = 0;
	} else {
	    sisr = 1;
	}
	lp->timeout = -1;
    }

    return sisr;
}

/*
** This function does 2 things: on Intels it kmalloc's another buffer to
** replace the one about to be passed up. On Alpha's it kmallocs a buffer
** into which the packet is copied.
*/
static struct sk_buff *
de4x5_alloc_rx_buff(struct net_device *dev, int index, int len)
{
    struct de4x5_private *lp = netdev_priv(dev);
    struct sk_buff *p;

#if !defined(__alpha__) && !defined(__powerpc__) && !defined(CONFIG_SPARC) && !defined(DE4X5_DO_MEMCPY)
    struct sk_buff *ret;
    u_long i=0, tmp;

    p = netdev_alloc_skb(dev, IEEE802_3_SZ + DE4X5_ALIGN + 2);
    if (!p) return NULL;

    tmp = virt_to_bus(p->data);
    i = ((tmp + DE4X5_ALIGN) & ~DE4X5_ALIGN) - tmp;
    skb_reserve(p, i);
    lp->rx_ring[index].buf = cpu_to_le32(tmp + i);

    ret = lp->rx_skb[index];
    lp->rx_skb[index] = p;

    if ((u_long) ret > 1) {
	skb_put(ret, len);
    }

    return ret;

#else
    if (lp->state != OPEN) return (struct sk_buff *)1; /* Fake out the open */

    p = netdev_alloc_skb(dev, len + 2);
    if (!p) return NULL;

    skb_reserve(p, 2);	                               /* Align */
    if (index < lp->rx_old) {                          /* Wrapped buffer */
	short tlen = (lp->rxRingSize - lp->rx_old) * RX_BUFF_SZ;
	memcpy(skb_put(p,tlen),lp->rx_bufs + lp->rx_old * RX_BUFF_SZ,tlen);
	memcpy(skb_put(p,len-tlen),lp->rx_bufs,len-tlen);
    } else {                                           /* Linear buffer */
	memcpy(skb_put(p,len),lp->rx_bufs + lp->rx_old * RX_BUFF_SZ,len);
    }

    return p;
#endif
}

static void
de4x5_free_rx_buffs(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    int i;

    for (i=0; i<lp->rxRingSize; i++) {
	if ((u_long) lp->rx_skb[i] > 1) {
	    dev_kfree_skb(lp->rx_skb[i]);
	}
	lp->rx_ring[i].status = 0;
	lp->rx_skb[i] = (struct sk_buff *)1;    /* Dummy entry */
    }
}

static void
de4x5_free_tx_buffs(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    int i;

    for (i=0; i<lp->txRingSize; i++) {
	if (lp->tx_skb[i])
	    de4x5_free_tx_buff(lp, i);
	lp->tx_ring[i].status = 0;
    }

    /* Unload the locally queued packets */
    __skb_queue_purge(&lp->cache.queue);
}

/*
** When a user pulls a connection, the DECchip can end up in a
** 'running - waiting for end of transmission' state. This means that we
** have to perform a chip soft reset to ensure that we can synchronize
** the hardware and software and make any media probes using a loopback
** packet meaningful.
*/
static void
de4x5_save_skbs(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;
    s32 omr;

    if (!lp->cache.save_cnt) {
	STOP_DE4X5;
	de4x5_tx(dev);                          /* Flush any sent skb's */
	de4x5_free_tx_buffs(dev);
	de4x5_cache_state(dev, DE4X5_SAVE_STATE);
	de4x5_sw_reset(dev);
	de4x5_cache_state(dev, DE4X5_RESTORE_STATE);
	lp->cache.save_cnt++;
	START_DE4X5;
    }
}

static void
de4x5_rst_desc_ring(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;
    int i;
    s32 omr;

    if (lp->cache.save_cnt) {
	STOP_DE4X5;
	outl(lp->dma_rings, DE4X5_RRBA);
	outl(lp->dma_rings + NUM_RX_DESC * sizeof(struct de4x5_desc),
	     DE4X5_TRBA);

	lp->rx_new = lp->rx_old = 0;
	lp->tx_new = lp->tx_old = 0;

	for (i = 0; i < lp->rxRingSize; i++) {
	    lp->rx_ring[i].status = cpu_to_le32(R_OWN);
	}

	for (i = 0; i < lp->txRingSize; i++) {
	    lp->tx_ring[i].status = cpu_to_le32(0);
	}

	barrier();
	lp->cache.save_cnt--;
	START_DE4X5;
    }
}

static void
de4x5_cache_state(struct net_device *dev, int flag)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;

    switch(flag) {
      case DE4X5_SAVE_STATE:
	lp->cache.csr0 = inl(DE4X5_BMR);
	lp->cache.csr6 = (inl(DE4X5_OMR) & ~(OMR_ST | OMR_SR));
	lp->cache.csr7 = inl(DE4X5_IMR);
	break;

      case DE4X5_RESTORE_STATE:
	outl(lp->cache.csr0, DE4X5_BMR);
	outl(lp->cache.csr6, DE4X5_OMR);
	outl(lp->cache.csr7, DE4X5_IMR);
	if (lp->chipset == DC21140) {
	    gep_wr(lp->cache.gepc, dev);
	    gep_wr(lp->cache.gep, dev);
	} else {
	    reset_init_sia(dev, lp->cache.csr13, lp->cache.csr14,
			                                      lp->cache.csr15);
	}
	break;
    }
}

static void
de4x5_put_cache(struct net_device *dev, struct sk_buff *skb)
{
    struct de4x5_private *lp = netdev_priv(dev);

    __skb_queue_tail(&lp->cache.queue, skb);
}

static void
de4x5_putb_cache(struct net_device *dev, struct sk_buff *skb)
{
    struct de4x5_private *lp = netdev_priv(dev);

    __skb_queue_head(&lp->cache.queue, skb);
}

static struct sk_buff *
de4x5_get_cache(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);

    return __skb_dequeue(&lp->cache.queue);
}

/*
** Check the Auto Negotiation State. Return OK when a link pass interrupt
** is received and the auto-negotiation status is NWAY OK.
*/
static int
test_ans(struct net_device *dev, s32 irqs, s32 irq_mask, s32 msec)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;
    s32 sts, ans;

    if (lp->timeout < 0) {
	lp->timeout = msec/100;
	outl(irq_mask, DE4X5_IMR);

	/* clear all pending interrupts */
	sts = inl(DE4X5_STS);
	outl(sts, DE4X5_STS);
    }

    ans = inl(DE4X5_SISR) & SISR_ANS;
    sts = inl(DE4X5_STS) & ~TIMER_CB;

    if (!(sts & irqs) && (ans ^ ANS_NWOK) && --lp->timeout) {
	sts = 100 | TIMER_CB;
    } else {
	lp->timeout = -1;
    }

    return sts;
}

static void
de4x5_setup_intr(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;
    s32 imr, sts;

    if (inl(DE4X5_OMR) & OMR_SR) {   /* Only unmask if TX/RX is enabled */
	imr = 0;
	UNMASK_IRQs;
	sts = inl(DE4X5_STS);        /* Reset any pending (stale) interrupts */
	outl(sts, DE4X5_STS);
	ENABLE_IRQs;
    }
}

/*
**
*/
static void
reset_init_sia(struct net_device *dev, s32 csr13, s32 csr14, s32 csr15)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;

    RESET_SIA;
    if (lp->useSROM) {
	if (lp->ibn == 3) {
	    srom_exec(dev, lp->phy[lp->active].rst);
	    srom_exec(dev, lp->phy[lp->active].gep);
	    outl(1, DE4X5_SICR);
	    return;
	} else {
	    csr15 = lp->cache.csr15;
	    csr14 = lp->cache.csr14;
	    csr13 = lp->cache.csr13;
	    outl(csr15 | lp->cache.gepc, DE4X5_SIGR);
	    outl(csr15 | lp->cache.gep, DE4X5_SIGR);
	}
    } else {
	outl(csr15, DE4X5_SIGR);
    }
    outl(csr14, DE4X5_STRR);
    outl(csr13, DE4X5_SICR);

    mdelay(10);
}

/*
** Create a loopback ethernet packet
*/
static void
create_packet(struct net_device *dev, char *frame, int len)
{
    int i;
    char *buf = frame;

    for (i=0; i<ETH_ALEN; i++) {             /* Use this source address */
	*buf++ = dev->dev_addr[i];
    }
    for (i=0; i<ETH_ALEN; i++) {             /* Use this destination address */
	*buf++ = dev->dev_addr[i];
    }

    *buf++ = 0;                              /* Packet length (2 bytes) */
    *buf++ = 1;
}

/*
** Look for a particular board name in the EISA configuration space
*/
static int
EISA_signature(char *name, struct device *device)
{
    int i, status = 0, siglen = ARRAY_SIZE(de4x5_signatures);
    struct eisa_device *edev;

    *name = '\0';
    edev = to_eisa_device (device);
    i = edev->id.driver_data;

    if (i >= 0 && i < siglen) {
	    strcpy (name, de4x5_signatures[i]);
	    status = 1;
    }

    return status;                         /* return the device name string */
}

/*
** Look for a particular board name in the PCI configuration space
*/
static int
PCI_signature(char *name, struct de4x5_private *lp)
{
    int i, status = 0, siglen = ARRAY_SIZE(de4x5_signatures);

    if (lp->chipset == DC21040) {
	strcpy(name, "DE434/5");
	return status;
    } else {                           /* Search for a DEC name in the SROM */
	int tmp = *((char *)&lp->srom + 19) * 3;
	strncpy(name, (char *)&lp->srom + 26 + tmp, 8);
    }
    name[8] = '\0';
    for (i=0; i<siglen; i++) {
	if (strstr(name,de4x5_signatures[i])!=NULL) break;
    }
    if (i == siglen) {
	if (dec_only) {
	    *name = '\0';
	} else {                        /* Use chip name to avoid confusion */
	    strcpy(name, (((lp->chipset == DC21040) ? "DC21040" :
			   ((lp->chipset == DC21041) ? "DC21041" :
			    ((lp->chipset == DC21140) ? "DC21140" :
			     ((lp->chipset == DC21142) ? "DC21142" :
			      ((lp->chipset == DC21143) ? "DC21143" : "UNKNOWN"
			     )))))));
	}
	if (lp->chipset != DC21041) {
	    lp->useSROM = true;             /* card is not recognisably DEC */
	}
    } else if ((lp->chipset & ~0x00ff) == DC2114x) {
	lp->useSROM = true;
    }

    return status;
}

/*
** Set up the Ethernet PROM counter to the start of the Ethernet address on
** the DC21040, else  read the SROM for the other chips.
** The SROM may not be present in a multi-MAC card, so first read the
** MAC address and check for a bad address. If there is a bad one then exit
** immediately with the prior srom contents intact (the h/w address will
** be fixed up later).
*/
static void
DevicePresent(struct net_device *dev, u_long aprom_addr)
{
    int i, j=0;
    struct de4x5_private *lp = netdev_priv(dev);

    if (lp->chipset == DC21040) {
	if (lp->bus == EISA) {
	    enet_addr_rst(aprom_addr); /* Reset Ethernet Address ROM Pointer */
	} else {
	    outl(0, aprom_addr);       /* Reset Ethernet Address ROM Pointer */
	}
    } else {                           /* Read new srom */
	u_short tmp;
	__le16 *p = (__le16 *)((char *)&lp->srom + SROM_HWADD);
	for (i=0; i<(ETH_ALEN>>1); i++) {
	    tmp = srom_rd(aprom_addr, (SROM_HWADD>>1) + i);
	    j += tmp;	/* for check for 0:0:0:0:0:0 or ff:ff:ff:ff:ff:ff */
	    *p = cpu_to_le16(tmp);
	}
	if (j == 0 || j == 3 * 0xffff) {
		/* could get 0 only from all-0 and 3 * 0xffff only from all-1 */
		return;
	}

	p = (__le16 *)&lp->srom;
	for (i=0; i<(sizeof(struct de4x5_srom)>>1); i++) {
	    tmp = srom_rd(aprom_addr, i);
	    *p++ = cpu_to_le16(tmp);
	}
	de4x5_dbg_srom(&lp->srom);
    }
}

/*
** Since the write on the Enet PROM register doesn't seem to reset the PROM
** pointer correctly (at least on my DE425 EISA card), this routine should do
** it...from depca.c.
*/
static void
enet_addr_rst(u_long aprom_addr)
{
    union {
	struct {
	    u32 a;
	    u32 b;
	} llsig;
	char Sig[sizeof(u32) << 1];
    } dev;
    short sigLength=0;
    s8 data;
    int i, j;

    dev.llsig.a = ETH_PROM_SIG;
    dev.llsig.b = ETH_PROM_SIG;
    sigLength = sizeof(u32) << 1;

    for (i=0,j=0;j<sigLength && i<PROBE_LENGTH+sigLength-1;i++) {
	data = inb(aprom_addr);
	if (dev.Sig[j] == data) {    /* track signature */
	    j++;
	} else {                     /* lost signature; begin search again */
	    if (data == dev.Sig[0]) {  /* rare case.... */
		j=1;
	    } else {
		j=0;
	    }
	}
    }
}

/*
** For the bad status case and no SROM, then add one to the previous
** address. However, need to add one backwards in case we have 0xff
** as one or more of the bytes. Only the last 3 bytes should be checked
** as the first three are invariant - assigned to an organisation.
*/
static int
get_hw_addr(struct net_device *dev)
{
    u_long iobase = dev->base_addr;
    int broken, i, k, tmp, status = 0;
    u_short j,chksum;
    struct de4x5_private *lp = netdev_priv(dev);

    broken = de4x5_bad_srom(lp);

    for (i=0,k=0,j=0;j<3;j++) {
	k <<= 1;
	if (k > 0xffff) k-=0xffff;

	if (lp->bus == PCI) {
	    if (lp->chipset == DC21040) {
		while ((tmp = inl(DE4X5_APROM)) < 0);
		k += (u_char) tmp;
		dev->dev_addr[i++] = (u_char) tmp;
		while ((tmp = inl(DE4X5_APROM)) < 0);
		k += (u_short) (tmp << 8);
		dev->dev_addr[i++] = (u_char) tmp;
	    } else if (!broken) {
		dev->dev_addr[i] = (u_char) lp->srom.ieee_addr[i]; i++;
		dev->dev_addr[i] = (u_char) lp->srom.ieee_addr[i]; i++;
	    } else if ((broken == SMC) || (broken == ACCTON)) {
		dev->dev_addr[i] = *((u_char *)&lp->srom + i); i++;
		dev->dev_addr[i] = *((u_char *)&lp->srom + i); i++;
	    }
	} else {
	    k += (u_char) (tmp = inb(EISA_APROM));
	    dev->dev_addr[i++] = (u_char) tmp;
	    k += (u_short) ((tmp = inb(EISA_APROM)) << 8);
	    dev->dev_addr[i++] = (u_char) tmp;
	}

	if (k > 0xffff) k-=0xffff;
    }
    if (k == 0xffff) k=0;

    if (lp->bus == PCI) {
	if (lp->chipset == DC21040) {
	    while ((tmp = inl(DE4X5_APROM)) < 0);
	    chksum = (u_char) tmp;
	    while ((tmp = inl(DE4X5_APROM)) < 0);
	    chksum |= (u_short) (tmp << 8);
	    if ((k != chksum) && (dec_only)) status = -1;
	}
    } else {
	chksum = (u_char) inb(EISA_APROM);
	chksum |= (u_short) (inb(EISA_APROM) << 8);
	if ((k != chksum) && (dec_only)) status = -1;
    }

    /* If possible, try to fix a broken card - SMC only so far */
    srom_repair(dev, broken);

#ifdef CONFIG_PPC_PMAC
    /*
    ** If the address starts with 00 a0, we have to bit-reverse
    ** each byte of the address.
    */
    if ( machine_is(powermac) &&
	 (dev->dev_addr[0] == 0) &&
	 (dev->dev_addr[1] == 0xa0) )
    {
	    for (i = 0; i < ETH_ALEN; ++i)
	    {
		    int x = dev->dev_addr[i];
		    x = ((x & 0xf) << 4) + ((x & 0xf0) >> 4);
		    x = ((x & 0x33) << 2) + ((x & 0xcc) >> 2);
		    dev->dev_addr[i] = ((x & 0x55) << 1) + ((x & 0xaa) >> 1);
	    }
    }
#endif /* CONFIG_PPC_PMAC */

    /* Test for a bad enet address */
    status = test_bad_enet(dev, status);

    return status;
}

/*
** Test for enet addresses in the first 32 bytes.
*/
static int
de4x5_bad_srom(struct de4x5_private *lp)
{
    int i, status = 0;

    for (i = 0; i < ARRAY_SIZE(enet_det); i++) {
	if (!memcmp(&lp->srom, &enet_det[i], 3) &&
	    !memcmp((char *)&lp->srom+0x10, &enet_det[i], 3)) {
	    if (i == 0) {
		status = SMC;
	    } else if (i == 1) {
		status = ACCTON;
	    }
	    break;
	}
    }

    return status;
}

static void
srom_repair(struct net_device *dev, int card)
{
    struct de4x5_private *lp = netdev_priv(dev);

    switch(card) {
      case SMC:
	memset((char *)&lp->srom, 0, sizeof(struct de4x5_srom));
	memcpy(lp->srom.ieee_addr, (char *)dev->dev_addr, ETH_ALEN);
	memcpy(lp->srom.info, (char *)&srom_repair_info[SMC-1], 100);
	lp->useSROM = true;
	break;
    }
}

/*
** Assume that the irq's do not follow the PCI spec - this is seems
** to be true so far (2 for 2).
*/
static int
test_bad_enet(struct net_device *dev, int status)
{
    struct de4x5_private *lp = netdev_priv(dev);
    int i, tmp;

    for (tmp=0,i=0; i<ETH_ALEN; i++) tmp += (u_char)dev->dev_addr[i];
    if ((tmp == 0) || (tmp == 0x5fa)) {
	if ((lp->chipset == last.chipset) &&
	    (lp->bus_num == last.bus) && (lp->bus_num > 0)) {
	    for (i=0; i<ETH_ALEN; i++) dev->dev_addr[i] = last.addr[i];
	    for (i=ETH_ALEN-1; i>2; --i) {
		dev->dev_addr[i] += 1;
		if (dev->dev_addr[i] != 0) break;
	    }
	    for (i=0; i<ETH_ALEN; i++) last.addr[i] = dev->dev_addr[i];
	    if (!an_exception(lp)) {
		dev->irq = last.irq;
	    }

	    status = 0;
	}
    } else if (!status) {
	last.chipset = lp->chipset;
	last.bus = lp->bus_num;
	last.irq = dev->irq;
	for (i=0; i<ETH_ALEN; i++) last.addr[i] = dev->dev_addr[i];
    }

    return status;
}

/*
** List of board exceptions with correctly wired IRQs
*/
static int
an_exception(struct de4x5_private *lp)
{
    if ((*(u_short *)lp->srom.sub_vendor_id == 0x00c0) &&
	(*(u_short *)lp->srom.sub_system_id == 0x95e0)) {
	return -1;
    }

    return 0;
}

/*
** SROM Read
*/
static short
srom_rd(u_long addr, u_char offset)
{
    sendto_srom(SROM_RD | SROM_SR, addr);

    srom_latch(SROM_RD | SROM_SR | DT_CS, addr);
    srom_command(SROM_RD | SROM_SR | DT_IN | DT_CS, addr);
    srom_address(SROM_RD | SROM_SR | DT_CS, addr, offset);

    return srom_data(SROM_RD | SROM_SR | DT_CS, addr);
}

static void
srom_latch(u_int command, u_long addr)
{
    sendto_srom(command, addr);
    sendto_srom(command | DT_CLK, addr);
    sendto_srom(command, addr);
}

static void
srom_command(u_int command, u_long addr)
{
    srom_latch(command, addr);
    srom_latch(command, addr);
    srom_latch((command & 0x0000ff00) | DT_CS, addr);
}

static void
srom_address(u_int command, u_long addr, u_char offset)
{
    int i, a;

    a = offset << 2;
    for (i=0; i<6; i++, a <<= 1) {
	srom_latch(command | ((a & 0x80) ? DT_IN : 0), addr);
    }
    udelay(1);

    i = (getfrom_srom(addr) >> 3) & 0x01;
}

static short
srom_data(u_int command, u_long addr)
{
    int i;
    short word = 0;
    s32 tmp;

    for (i=0; i<16; i++) {
	sendto_srom(command  | DT_CLK, addr);
	tmp = getfrom_srom(addr);
	sendto_srom(command, addr);

	word = (word << 1) | ((tmp >> 3) & 0x01);
    }

    sendto_srom(command & 0x0000ff00, addr);

    return word;
}

/*
static void
srom_busy(u_int command, u_long addr)
{
   sendto_srom((command & 0x0000ff00) | DT_CS, addr);

   while (!((getfrom_srom(addr) >> 3) & 0x01)) {
       mdelay(1);
   }

   sendto_srom(command & 0x0000ff00, addr);
}
*/

static void
sendto_srom(u_int command, u_long addr)
{
    outl(command, addr);
    udelay(1);
}

static int
getfrom_srom(u_long addr)
{
    s32 tmp;

    tmp = inl(addr);
    udelay(1);

    return tmp;
}

static int
srom_infoleaf_info(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    int i, count;
    u_char *p;

    /* Find the infoleaf decoder function that matches this chipset */
    for (i=0; i<INFOLEAF_SIZE; i++) {
	if (lp->chipset == infoleaf_array[i].chipset) break;
    }
    if (i == INFOLEAF_SIZE) {
	lp->useSROM = false;
	printk("%s: Cannot find correct chipset for SROM decoding!\n",
	                                                          dev->name);
	return -ENXIO;
    }

    lp->infoleaf_fn = infoleaf_array[i].fn;

    /* Find the information offset that this function should use */
    count = *((u_char *)&lp->srom + 19);
    p  = (u_char *)&lp->srom + 26;

    if (count > 1) {
	for (i=count; i; --i, p+=3) {
	    if (lp->device == *p) break;
	}
	if (i == 0) {
	    lp->useSROM = false;
	    printk("%s: Cannot find correct PCI device [%d] for SROM decoding!\n",
	                                               dev->name, lp->device);
	    return -ENXIO;
	}
    }

	lp->infoleaf_offset = get_unaligned_le16(p + 1);

    return 0;
}

/*
** This routine loads any type 1 or 3 MII info into the mii device
** struct and executes any type 5 code to reset PHY devices for this
** controller.
** The info for the MII devices will be valid since the index used
** will follow the discovery process from MII address 1-31 then 0.
*/
static void
srom_init(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_char *p = (u_char *)&lp->srom + lp->infoleaf_offset;
    u_char count;

    p+=2;
    if (lp->chipset == DC21140) {
	lp->cache.gepc = (*p++ | GEP_CTRL);
	gep_wr(lp->cache.gepc, dev);
    }

    /* Block count */
    count = *p++;

    /* Jump the infoblocks to find types */
    for (;count; --count) {
	if (*p < 128) {
	    p += COMPACT_LEN;
	} else if (*(p+1) == 5) {
	    type5_infoblock(dev, 1, p);
	    p += ((*p & BLOCK_LEN) + 1);
	} else if (*(p+1) == 4) {
	    p += ((*p & BLOCK_LEN) + 1);
	} else if (*(p+1) == 3) {
	    type3_infoblock(dev, 1, p);
	    p += ((*p & BLOCK_LEN) + 1);
	} else if (*(p+1) == 2) {
	    p += ((*p & BLOCK_LEN) + 1);
	} else if (*(p+1) == 1) {
	    type1_infoblock(dev, 1, p);
	    p += ((*p & BLOCK_LEN) + 1);
	} else {
	    p += ((*p & BLOCK_LEN) + 1);
	}
    }
}

/*
** A generic routine that writes GEP control, data and reset information
** to the GEP register (21140) or csr15 GEP portion (2114[23]).
*/
static void
srom_exec(struct net_device *dev, u_char *p)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;
    u_char count = (p ? *p++ : 0);
    u_short *w = (u_short *)p;

    if (((lp->ibn != 1) && (lp->ibn != 3) && (lp->ibn != 5)) || !count) return;

    if (lp->chipset != DC21140) RESET_SIA;

    while (count--) {
	gep_wr(((lp->chipset==DC21140) && (lp->ibn!=5) ?
		                                   *p++ : get_unaligned_le16(w++)), dev);
	mdelay(2);                          /* 2ms per action */
    }

    if (lp->chipset != DC21140) {
	outl(lp->cache.csr14, DE4X5_STRR);
	outl(lp->cache.csr13, DE4X5_SICR);
    }
}

/*
** Basically this function is a NOP since it will never be called,
** unless I implement the DC21041 SROM functions. There's no need
** since the existing code will be satisfactory for all boards.
*/
static int
dc21041_infoleaf(struct net_device *dev)
{
    return DE4X5_AUTOSENSE_MS;
}

static int
dc21140_infoleaf(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_char count = 0;
    u_char *p = (u_char *)&lp->srom + lp->infoleaf_offset;
    int next_tick = DE4X5_AUTOSENSE_MS;

    /* Read the connection type */
    p+=2;

    /* GEP control */
    lp->cache.gepc = (*p++ | GEP_CTRL);

    /* Block count */
    count = *p++;

    /* Recursively figure out the info blocks */
    if (*p < 128) {
	next_tick = dc_infoblock[COMPACT](dev, count, p);
    } else {
	next_tick = dc_infoblock[*(p+1)](dev, count, p);
    }

    if (lp->tcount == count) {
	lp->media = NC;
        if (lp->media != lp->c_media) {
	    de4x5_dbg_media(dev);
	    lp->c_media = lp->media;
	}
	lp->media = INIT;
	lp->tcount = 0;
	lp->tx_enable = false;
    }

    return next_tick & ~TIMER_CB;
}

static int
dc21142_infoleaf(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_char count = 0;
    u_char *p = (u_char *)&lp->srom + lp->infoleaf_offset;
    int next_tick = DE4X5_AUTOSENSE_MS;

    /* Read the connection type */
    p+=2;

    /* Block count */
    count = *p++;

    /* Recursively figure out the info blocks */
    if (*p < 128) {
	next_tick = dc_infoblock[COMPACT](dev, count, p);
    } else {
	next_tick = dc_infoblock[*(p+1)](dev, count, p);
    }

    if (lp->tcount == count) {
	lp->media = NC;
        if (lp->media != lp->c_media) {
	    de4x5_dbg_media(dev);
	    lp->c_media = lp->media;
	}
	lp->media = INIT;
	lp->tcount = 0;
	lp->tx_enable = false;
    }

    return next_tick & ~TIMER_CB;
}

static int
dc21143_infoleaf(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_char count = 0;
    u_char *p = (u_char *)&lp->srom + lp->infoleaf_offset;
    int next_tick = DE4X5_AUTOSENSE_MS;

    /* Read the connection type */
    p+=2;

    /* Block count */
    count = *p++;

    /* Recursively figure out the info blocks */
    if (*p < 128) {
	next_tick = dc_infoblock[COMPACT](dev, count, p);
    } else {
	next_tick = dc_infoblock[*(p+1)](dev, count, p);
    }
    if (lp->tcount == count) {
	lp->media = NC;
        if (lp->media != lp->c_media) {
	    de4x5_dbg_media(dev);
	    lp->c_media = lp->media;
	}
	lp->media = INIT;
	lp->tcount = 0;
	lp->tx_enable = false;
    }

    return next_tick & ~TIMER_CB;
}

/*
** The compact infoblock is only designed for DC21140[A] chips, so
** we'll reuse the dc21140m_autoconf function. Non MII media only.
*/
static int
compact_infoblock(struct net_device *dev, u_char count, u_char *p)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_char flags, csr6;

    /* Recursively figure out the info blocks */
    if (--count > lp->tcount) {
	if (*(p+COMPACT_LEN) < 128) {
	    return dc_infoblock[COMPACT](dev, count, p+COMPACT_LEN);
	} else {
	    return dc_infoblock[*(p+COMPACT_LEN+1)](dev, count, p+COMPACT_LEN);
	}
    }

    if ((lp->media == INIT) && (lp->timeout < 0)) {
        lp->ibn = COMPACT;
        lp->active = 0;
	gep_wr(lp->cache.gepc, dev);
	lp->infoblock_media = (*p++) & COMPACT_MC;
	lp->cache.gep = *p++;
	csr6 = *p++;
	flags = *p++;

	lp->asBitValid = (flags & 0x80) ? 0 : -1;
	lp->defMedium = (flags & 0x40) ? -1 : 0;
	lp->asBit = 1 << ((csr6 >> 1) & 0x07);
	lp->asPolarity = ((csr6 & 0x80) ? -1 : 0) & lp->asBit;
	lp->infoblock_csr6 = OMR_DEF | ((csr6 & 0x71) << 18);
	lp->useMII = false;

	de4x5_switch_mac_port(dev);
    }

    return dc21140m_autoconf(dev);
}

/*
** This block describes non MII media for the DC21140[A] only.
*/
static int
type0_infoblock(struct net_device *dev, u_char count, u_char *p)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_char flags, csr6, len = (*p & BLOCK_LEN)+1;

    /* Recursively figure out the info blocks */
    if (--count > lp->tcount) {
	if (*(p+len) < 128) {
	    return dc_infoblock[COMPACT](dev, count, p+len);
	} else {
	    return dc_infoblock[*(p+len+1)](dev, count, p+len);
	}
    }

    if ((lp->media == INIT) && (lp->timeout < 0)) {
        lp->ibn = 0;
        lp->active = 0;
        gep_wr(lp->cache.gepc, dev);
	p+=2;
	lp->infoblock_media = (*p++) & BLOCK0_MC;
	lp->cache.gep = *p++;
	csr6 = *p++;
	flags = *p++;

	lp->asBitValid = (flags & 0x80) ? 0 : -1;
	lp->defMedium = (flags & 0x40) ? -1 : 0;
	lp->asBit = 1 << ((csr6 >> 1) & 0x07);
	lp->asPolarity = ((csr6 & 0x80) ? -1 : 0) & lp->asBit;
	lp->infoblock_csr6 = OMR_DEF | ((csr6 & 0x71) << 18);
	lp->useMII = false;

	de4x5_switch_mac_port(dev);
    }

    return dc21140m_autoconf(dev);
}

/* These functions are under construction! */

static int
type1_infoblock(struct net_device *dev, u_char count, u_char *p)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_char len = (*p & BLOCK_LEN)+1;

    /* Recursively figure out the info blocks */
    if (--count > lp->tcount) {
	if (*(p+len) < 128) {
	    return dc_infoblock[COMPACT](dev, count, p+len);
	} else {
	    return dc_infoblock[*(p+len+1)](dev, count, p+len);
	}
    }

    p += 2;
    if (lp->state == INITIALISED) {
        lp->ibn = 1;
	lp->active = *p++;
	lp->phy[lp->active].gep = (*p ? p : NULL); p += (*p + 1);
	lp->phy[lp->active].rst = (*p ? p : NULL); p += (*p + 1);
	lp->phy[lp->active].mc  = get_unaligned_le16(p); p += 2;
	lp->phy[lp->active].ana = get_unaligned_le16(p); p += 2;
	lp->phy[lp->active].fdx = get_unaligned_le16(p); p += 2;
	lp->phy[lp->active].ttm = get_unaligned_le16(p);
	return 0;
    } else if ((lp->media == INIT) && (lp->timeout < 0)) {
        lp->ibn = 1;
        lp->active = *p;
	lp->infoblock_csr6 = OMR_MII_100;
	lp->useMII = true;
	lp->infoblock_media = ANS;

	de4x5_switch_mac_port(dev);
    }

    return dc21140m_autoconf(dev);
}

static int
type2_infoblock(struct net_device *dev, u_char count, u_char *p)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_char len = (*p & BLOCK_LEN)+1;

    /* Recursively figure out the info blocks */
    if (--count > lp->tcount) {
	if (*(p+len) < 128) {
	    return dc_infoblock[COMPACT](dev, count, p+len);
	} else {
	    return dc_infoblock[*(p+len+1)](dev, count, p+len);
	}
    }

    if ((lp->media == INIT) && (lp->timeout < 0)) {
        lp->ibn = 2;
        lp->active = 0;
	p += 2;
	lp->infoblock_media = (*p) & MEDIA_CODE;

        if ((*p++) & EXT_FIELD) {
	    lp->cache.csr13 = get_unaligned_le16(p); p += 2;
	    lp->cache.csr14 = get_unaligned_le16(p); p += 2;
	    lp->cache.csr15 = get_unaligned_le16(p); p += 2;
	} else {
	    lp->cache.csr13 = CSR13;
	    lp->cache.csr14 = CSR14;
	    lp->cache.csr15 = CSR15;
	}
        lp->cache.gepc = ((s32)(get_unaligned_le16(p)) << 16); p += 2;
        lp->cache.gep  = ((s32)(get_unaligned_le16(p)) << 16);
	lp->infoblock_csr6 = OMR_SIA;
	lp->useMII = false;

	de4x5_switch_mac_port(dev);
    }

    return dc2114x_autoconf(dev);
}

static int
type3_infoblock(struct net_device *dev, u_char count, u_char *p)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_char len = (*p & BLOCK_LEN)+1;

    /* Recursively figure out the info blocks */
    if (--count > lp->tcount) {
	if (*(p+len) < 128) {
	    return dc_infoblock[COMPACT](dev, count, p+len);
	} else {
	    return dc_infoblock[*(p+len+1)](dev, count, p+len);
	}
    }

    p += 2;
    if (lp->state == INITIALISED) {
        lp->ibn = 3;
        lp->active = *p++;
	if (MOTO_SROM_BUG) lp->active = 0;
	lp->phy[lp->active].gep = (*p ? p : NULL); p += (2 * (*p) + 1);
	lp->phy[lp->active].rst = (*p ? p : NULL); p += (2 * (*p) + 1);
	lp->phy[lp->active].mc  = get_unaligned_le16(p); p += 2;
	lp->phy[lp->active].ana = get_unaligned_le16(p); p += 2;
	lp->phy[lp->active].fdx = get_unaligned_le16(p); p += 2;
	lp->phy[lp->active].ttm = get_unaligned_le16(p); p += 2;
	lp->phy[lp->active].mci = *p;
	return 0;
    } else if ((lp->media == INIT) && (lp->timeout < 0)) {
        lp->ibn = 3;
	lp->active = *p;
	if (MOTO_SROM_BUG) lp->active = 0;
	lp->infoblock_csr6 = OMR_MII_100;
	lp->useMII = true;
	lp->infoblock_media = ANS;

	de4x5_switch_mac_port(dev);
    }

    return dc2114x_autoconf(dev);
}

static int
type4_infoblock(struct net_device *dev, u_char count, u_char *p)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_char flags, csr6, len = (*p & BLOCK_LEN)+1;

    /* Recursively figure out the info blocks */
    if (--count > lp->tcount) {
	if (*(p+len) < 128) {
	    return dc_infoblock[COMPACT](dev, count, p+len);
	} else {
	    return dc_infoblock[*(p+len+1)](dev, count, p+len);
	}
    }

    if ((lp->media == INIT) && (lp->timeout < 0)) {
        lp->ibn = 4;
        lp->active = 0;
	p+=2;
	lp->infoblock_media = (*p++) & MEDIA_CODE;
        lp->cache.csr13 = CSR13;              /* Hard coded defaults */
	lp->cache.csr14 = CSR14;
	lp->cache.csr15 = CSR15;
        lp->cache.gepc = ((s32)(get_unaligned_le16(p)) << 16); p += 2;
        lp->cache.gep  = ((s32)(get_unaligned_le16(p)) << 16); p += 2;
	csr6 = *p++;
	flags = *p++;

	lp->asBitValid = (flags & 0x80) ? 0 : -1;
	lp->defMedium = (flags & 0x40) ? -1 : 0;
	lp->asBit = 1 << ((csr6 >> 1) & 0x07);
	lp->asPolarity = ((csr6 & 0x80) ? -1 : 0) & lp->asBit;
	lp->infoblock_csr6 = OMR_DEF | ((csr6 & 0x71) << 18);
	lp->useMII = false;

	de4x5_switch_mac_port(dev);
    }

    return dc2114x_autoconf(dev);
}

/*
** This block type provides information for resetting external devices
** (chips) through the General Purpose Register.
*/
static int
type5_infoblock(struct net_device *dev, u_char count, u_char *p)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_char len = (*p & BLOCK_LEN)+1;

    /* Recursively figure out the info blocks */
    if (--count > lp->tcount) {
	if (*(p+len) < 128) {
	    return dc_infoblock[COMPACT](dev, count, p+len);
	} else {
	    return dc_infoblock[*(p+len+1)](dev, count, p+len);
	}
    }

    /* Must be initializing to run this code */
    if ((lp->state == INITIALISED) || (lp->media == INIT)) {
	p+=2;
        lp->rst = p;
        srom_exec(dev, lp->rst);
    }

    return DE4X5_AUTOSENSE_MS;
}

/*
** MII Read/Write
*/

static int
mii_rd(u_char phyreg, u_char phyaddr, u_long ioaddr)
{
    mii_wdata(MII_PREAMBLE,  2, ioaddr);   /* Start of 34 bit preamble...    */
    mii_wdata(MII_PREAMBLE, 32, ioaddr);   /* ...continued                   */
    mii_wdata(MII_STRD, 4, ioaddr);        /* SFD and Read operation         */
    mii_address(phyaddr, ioaddr);          /* PHY address to be accessed     */
    mii_address(phyreg, ioaddr);           /* PHY Register to read           */
    mii_ta(MII_STRD, ioaddr);              /* Turn around time - 2 MDC       */

    return mii_rdata(ioaddr);              /* Read data                      */
}

static void
mii_wr(int data, u_char phyreg, u_char phyaddr, u_long ioaddr)
{
    mii_wdata(MII_PREAMBLE,  2, ioaddr);   /* Start of 34 bit preamble...    */
    mii_wdata(MII_PREAMBLE, 32, ioaddr);   /* ...continued                   */
    mii_wdata(MII_STWR, 4, ioaddr);        /* SFD and Write operation        */
    mii_address(phyaddr, ioaddr);          /* PHY address to be accessed     */
    mii_address(phyreg, ioaddr);           /* PHY Register to write          */
    mii_ta(MII_STWR, ioaddr);              /* Turn around time - 2 MDC       */
    data = mii_swap(data, 16);             /* Swap data bit ordering         */
    mii_wdata(data, 16, ioaddr);           /* Write data                     */
}

static int
mii_rdata(u_long ioaddr)
{
    int i;
    s32 tmp = 0;

    for (i=0; i<16; i++) {
	tmp <<= 1;
	tmp |= getfrom_mii(MII_MRD | MII_RD, ioaddr);
    }

    return tmp;
}

static void
mii_wdata(int data, int len, u_long ioaddr)
{
    int i;

    for (i=0; i<len; i++) {
	sendto_mii(MII_MWR | MII_WR, data, ioaddr);
	data >>= 1;
    }
}

static void
mii_address(u_char addr, u_long ioaddr)
{
    int i;

    addr = mii_swap(addr, 5);
    for (i=0; i<5; i++) {
	sendto_mii(MII_MWR | MII_WR, addr, ioaddr);
	addr >>= 1;
    }
}

static void
mii_ta(u_long rw, u_long ioaddr)
{
    if (rw == MII_STWR) {
	sendto_mii(MII_MWR | MII_WR, 1, ioaddr);
	sendto_mii(MII_MWR | MII_WR, 0, ioaddr);
    } else {
	getfrom_mii(MII_MRD | MII_RD, ioaddr);        /* Tri-state MDIO */
    }
}

static int
mii_swap(int data, int len)
{
    int i, tmp = 0;

    for (i=0; i<len; i++) {
	tmp <<= 1;
	tmp |= (data & 1);
	data >>= 1;
    }

    return tmp;
}

static void
sendto_mii(u32 command, int data, u_long ioaddr)
{
    u32 j;

    j = (data & 1) << 17;
    outl(command | j, ioaddr);
    udelay(1);
    outl(command | MII_MDC | j, ioaddr);
    udelay(1);
}

static int
getfrom_mii(u32 command, u_long ioaddr)
{
    outl(command, ioaddr);
    udelay(1);
    outl(command | MII_MDC, ioaddr);
    udelay(1);

    return (inl(ioaddr) >> 19) & 1;
}

/*
** Here's 3 ways to calculate the OUI from the ID registers.
*/
static int
mii_get_oui(u_char phyaddr, u_long ioaddr)
{
/*
    union {
	u_short reg;
	u_char breg[2];
    } a;
    int i, r2, r3, ret=0;*/
    int r2, r3;

    /* Read r2 and r3 */
    r2 = mii_rd(MII_ID0, phyaddr, ioaddr);
    r3 = mii_rd(MII_ID1, phyaddr, ioaddr);
                                                /* SEEQ and Cypress way * /
    / * Shuffle r2 and r3 * /
    a.reg=0;
    r3 = ((r3>>10)|(r2<<6))&0x0ff;
    r2 = ((r2>>2)&0x3fff);

    / * Bit reverse r3 * /
    for (i=0;i<8;i++) {
	ret<<=1;
	ret |= (r3&1);
	r3>>=1;
    }

    / * Bit reverse r2 * /
    for (i=0;i<16;i++) {
	a.reg<<=1;
	a.reg |= (r2&1);
	r2>>=1;
    }

    / * Swap r2 bytes * /
    i=a.breg[0];
    a.breg[0]=a.breg[1];
    a.breg[1]=i;

    return (a.reg<<8)|ret; */                 /* SEEQ and Cypress way */
/*    return (r2<<6)|(u_int)(r3>>10); */      /* NATIONAL and BROADCOM way */
    return r2;                                  /* (I did it) My way */
}

/*
** The SROM spec forces us to search addresses [1-31 0]. Bummer.
*/
static int
mii_get_phy(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;
    int i, j, k, n, limit=ARRAY_SIZE(phy_info);
    int id;

    lp->active = 0;
    lp->useMII = true;

    /* Search the MII address space for possible PHY devices */
    for (n=0, lp->mii_cnt=0, i=1; !((i==1) && (n==1)); i=(i+1)%DE4X5_MAX_MII) {
	lp->phy[lp->active].addr = i;
	if (i==0) n++;                             /* Count cycles */
	while (de4x5_reset_phy(dev)<0) udelay(100);/* Wait for reset */
	id = mii_get_oui(i, DE4X5_MII);
	if ((id == 0) || (id == 65535)) continue;  /* Valid ID? */
	for (j=0; j<limit; j++) {                  /* Search PHY table */
	    if (id != phy_info[j].id) continue;    /* ID match? */
	    for (k=0; k < DE4X5_MAX_PHY && lp->phy[k].id; k++);
	    if (k < DE4X5_MAX_PHY) {
		memcpy((char *)&lp->phy[k],
		       (char *)&phy_info[j], sizeof(struct phy_table));
		lp->phy[k].addr = i;
		lp->mii_cnt++;
		lp->active++;
	    } else {
		goto purgatory;                    /* Stop the search */
	    }
	    break;
	}
	if ((j == limit) && (i < DE4X5_MAX_MII)) {
	    for (k=0; k < DE4X5_MAX_PHY && lp->phy[k].id; k++);
	    lp->phy[k].addr = i;
	    lp->phy[k].id = id;
	    lp->phy[k].spd.reg = GENERIC_REG;      /* ANLPA register         */
	    lp->phy[k].spd.mask = GENERIC_MASK;    /* 100Mb/s technologies   */
	    lp->phy[k].spd.value = GENERIC_VALUE;  /* TX & T4, H/F Duplex    */
	    lp->mii_cnt++;
	    lp->active++;
	    printk("%s: Using generic MII device control. If the board doesn't operate,\nplease mail the following dump to the author:\n", dev->name);
	    j = de4x5_debug;
	    de4x5_debug |= DEBUG_MII;
	    de4x5_dbg_mii(dev, k);
	    de4x5_debug = j;
	    printk("\n");
	}
    }
  purgatory:
    lp->active = 0;
    if (lp->phy[0].id) {                           /* Reset the PHY devices */
	for (k=0; k < DE4X5_MAX_PHY && lp->phy[k].id; k++) { /*For each PHY*/
	    mii_wr(MII_CR_RST, MII_CR, lp->phy[k].addr, DE4X5_MII);
	    while (mii_rd(MII_CR, lp->phy[k].addr, DE4X5_MII) & MII_CR_RST);

	    de4x5_dbg_mii(dev, k);
	}
    }
    if (!lp->mii_cnt) lp->useMII = false;

    return lp->mii_cnt;
}

static char *
build_setup_frame(struct net_device *dev, int mode)
{
    struct de4x5_private *lp = netdev_priv(dev);
    int i;
    char *pa = lp->setup_frame;

    /* Initialise the setup frame */
    if (mode == ALL) {
	memset(lp->setup_frame, 0, SETUP_FRAME_LEN);
    }

    if (lp->setup_f == HASH_PERF) {
	for (pa=lp->setup_frame+IMPERF_PA_OFFSET, i=0; i<ETH_ALEN; i++) {
	    *(pa + i) = dev->dev_addr[i];                 /* Host address */
	    if (i & 0x01) pa += 2;
	}
	*(lp->setup_frame + (DE4X5_HASH_TABLE_LEN >> 3) - 3) = 0x80;
    } else {
	for (i=0; i<ETH_ALEN; i++) { /* Host address */
	    *(pa + (i&1)) = dev->dev_addr[i];
	    if (i & 0x01) pa += 4;
	}
	for (i=0; i<ETH_ALEN; i++) { /* Broadcast address */
	    *(pa + (i&1)) = (char) 0xff;
	    if (i & 0x01) pa += 4;
	}
    }

    return pa;                     /* Points to the next entry */
}

static void
disable_ast(struct net_device *dev)
{
	struct de4x5_private *lp = netdev_priv(dev);
	del_timer_sync(&lp->timer);
}

static long
de4x5_switch_mac_port(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;
    s32 omr;

    STOP_DE4X5;

    /* Assert the OMR_PS bit in CSR6 */
    omr = (inl(DE4X5_OMR) & ~(OMR_PS | OMR_HBD | OMR_TTM | OMR_PCS | OMR_SCR |
			                                             OMR_FDX));
    omr |= lp->infoblock_csr6;
    if (omr & OMR_PS) omr |= OMR_HBD;
    outl(omr, DE4X5_OMR);

    /* Soft Reset */
    RESET_DE4X5;

    /* Restore the GEP - especially for COMPACT and Type 0 Infoblocks */
    if (lp->chipset == DC21140) {
	gep_wr(lp->cache.gepc, dev);
	gep_wr(lp->cache.gep, dev);
    } else if ((lp->chipset & ~0x0ff) == DC2114x) {
	reset_init_sia(dev, lp->cache.csr13, lp->cache.csr14, lp->cache.csr15);
    }

    /* Restore CSR6 */
    outl(omr, DE4X5_OMR);

    /* Reset CSR8 */
    inl(DE4X5_MFC);

    return omr;
}

static void
gep_wr(s32 data, struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;

    if (lp->chipset == DC21140) {
	outl(data, DE4X5_GEP);
    } else if ((lp->chipset & ~0x00ff) == DC2114x) {
	outl((data<<16) | lp->cache.csr15, DE4X5_SIGR);
    }
}

static int
gep_rd(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;

    if (lp->chipset == DC21140) {
	return inl(DE4X5_GEP);
    } else if ((lp->chipset & ~0x00ff) == DC2114x) {
	return inl(DE4X5_SIGR) & 0x000fffff;
    }

    return 0;
}

static void
yawn(struct net_device *dev, int state)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;

    if ((lp->chipset == DC21040) || (lp->chipset == DC21140)) return;

    if(lp->bus == EISA) {
	switch(state) {
	  case WAKEUP:
	    outb(WAKEUP, PCI_CFPM);
	    mdelay(10);
	    break;

	  case SNOOZE:
	    outb(SNOOZE, PCI_CFPM);
	    break;

	  case SLEEP:
	    outl(0, DE4X5_SICR);
	    outb(SLEEP, PCI_CFPM);
	    break;
	}
    } else {
	struct pci_dev *pdev = to_pci_dev (lp->gendev);
	switch(state) {
	  case WAKEUP:
	    pci_write_config_byte(pdev, PCI_CFDA_PSM, WAKEUP);
	    mdelay(10);
	    break;

	  case SNOOZE:
	    pci_write_config_byte(pdev, PCI_CFDA_PSM, SNOOZE);
	    break;

	  case SLEEP:
	    outl(0, DE4X5_SICR);
	    pci_write_config_byte(pdev, PCI_CFDA_PSM, SLEEP);
	    break;
	}
    }
}

static void
de4x5_parse_params(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    char *p, *q, t;

    lp->params.fdx = false;
    lp->params.autosense = AUTO;

    if (args == NULL) return;

    if ((p = strstr(args, dev->name))) {
	if (!(q = strstr(p+strlen(dev->name), "eth"))) q = p + strlen(p);
	t = *q;
	*q = '\0';

	if (strstr(p, "fdx") || strstr(p, "FDX")) lp->params.fdx = true;

	if (strstr(p, "autosense") || strstr(p, "AUTOSENSE")) {
	    if (strstr(p, "TP_NW")) {
		lp->params.autosense = TP_NW;
	    } else if (strstr(p, "TP")) {
		lp->params.autosense = TP;
	    } else if (strstr(p, "BNC_AUI")) {
		lp->params.autosense = BNC;
	    } else if (strstr(p, "BNC")) {
		lp->params.autosense = BNC;
	    } else if (strstr(p, "AUI")) {
		lp->params.autosense = AUI;
	    } else if (strstr(p, "10Mb")) {
		lp->params.autosense = _10Mb;
	    } else if (strstr(p, "100Mb")) {
		lp->params.autosense = _100Mb;
	    } else if (strstr(p, "AUTO")) {
		lp->params.autosense = AUTO;
	    }
	}
	*q = t;
    }
}

static void
de4x5_dbg_open(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);
    int i;

    if (de4x5_debug & DEBUG_OPEN) {
	printk("%s: de4x5 opening with irq %d\n",dev->name,dev->irq);
	printk("\tphysical address: %pM\n", dev->dev_addr);
	printk("Descriptor head addresses:\n");
	printk("\t0x%8.8lx  0x%8.8lx\n",(u_long)lp->rx_ring,(u_long)lp->tx_ring);
	printk("Descriptor addresses:\nRX: ");
	for (i=0;i<lp->rxRingSize-1;i++){
	    if (i < 3) {
		printk("0x%8.8lx  ",(u_long)&lp->rx_ring[i].status);
	    }
	}
	printk("...0x%8.8lx\n",(u_long)&lp->rx_ring[i].status);
	printk("TX: ");
	for (i=0;i<lp->txRingSize-1;i++){
	    if (i < 3) {
		printk("0x%8.8lx  ", (u_long)&lp->tx_ring[i].status);
	    }
	}
	printk("...0x%8.8lx\n", (u_long)&lp->tx_ring[i].status);
	printk("Descriptor buffers:\nRX: ");
	for (i=0;i<lp->rxRingSize-1;i++){
	    if (i < 3) {
		printk("0x%8.8x  ",le32_to_cpu(lp->rx_ring[i].buf));
	    }
	}
	printk("...0x%8.8x\n",le32_to_cpu(lp->rx_ring[i].buf));
	printk("TX: ");
	for (i=0;i<lp->txRingSize-1;i++){
	    if (i < 3) {
		printk("0x%8.8x  ", le32_to_cpu(lp->tx_ring[i].buf));
	    }
	}
	printk("...0x%8.8x\n", le32_to_cpu(lp->tx_ring[i].buf));
	printk("Ring size:\nRX: %d\nTX: %d\n",
	       (short)lp->rxRingSize,
	       (short)lp->txRingSize);
    }
}

static void
de4x5_dbg_mii(struct net_device *dev, int k)
{
    struct de4x5_private *lp = netdev_priv(dev);
    u_long iobase = dev->base_addr;

    if (de4x5_debug & DEBUG_MII) {
	printk("\nMII device address: %d\n", lp->phy[k].addr);
	printk("MII CR:  %x\n",mii_rd(MII_CR,lp->phy[k].addr,DE4X5_MII));
	printk("MII SR:  %x\n",mii_rd(MII_SR,lp->phy[k].addr,DE4X5_MII));
	printk("MII ID0: %x\n",mii_rd(MII_ID0,lp->phy[k].addr,DE4X5_MII));
	printk("MII ID1: %x\n",mii_rd(MII_ID1,lp->phy[k].addr,DE4X5_MII));
	if (lp->phy[k].id != BROADCOM_T4) {
	    printk("MII ANA: %x\n",mii_rd(0x04,lp->phy[k].addr,DE4X5_MII));
	    printk("MII ANC: %x\n",mii_rd(0x05,lp->phy[k].addr,DE4X5_MII));
	}
	printk("MII 16:  %x\n",mii_rd(0x10,lp->phy[k].addr,DE4X5_MII));
	if (lp->phy[k].id != BROADCOM_T4) {
	    printk("MII 17:  %x\n",mii_rd(0x11,lp->phy[k].addr,DE4X5_MII));
	    printk("MII 18:  %x\n",mii_rd(0x12,lp->phy[k].addr,DE4X5_MII));
	} else {
	    printk("MII 20:  %x\n",mii_rd(0x14,lp->phy[k].addr,DE4X5_MII));
	}
    }
}

static void
de4x5_dbg_media(struct net_device *dev)
{
    struct de4x5_private *lp = netdev_priv(dev);

    if (lp->media != lp->c_media) {
	if (de4x5_debug & DEBUG_MEDIA) {
	    printk("%s: media is %s%s\n", dev->name,
		   (lp->media == NC  ? "unconnected, link down or incompatible connection" :
		    (lp->media == TP  ? "TP" :
		     (lp->media == ANS ? "TP/Nway" :
		      (lp->media == BNC ? "BNC" :
		       (lp->media == AUI ? "AUI" :
			(lp->media == BNC_AUI ? "BNC/AUI" :
			 (lp->media == EXT_SIA ? "EXT SIA" :
			  (lp->media == _100Mb  ? "100Mb/s" :
			   (lp->media == _10Mb   ? "10Mb/s" :
			    "???"
			    ))))))))), (lp->fdx?" full duplex.":"."));
	}
	lp->c_media = lp->media;
    }
}

static void
de4x5_dbg_srom(struct de4x5_srom *p)
{
    int i;

    if (de4x5_debug & DEBUG_SROM) {
	printk("Sub-system Vendor ID: %04x\n", *((u_short *)p->sub_vendor_id));
	printk("Sub-system ID:        %04x\n", *((u_short *)p->sub_system_id));
	printk("ID Block CRC:         %02x\n", (u_char)(p->id_block_crc));
	printk("SROM version:         %02x\n", (u_char)(p->version));
	printk("# controllers:        %02x\n", (u_char)(p->num_controllers));

	printk("Hardware Address:     %pM\n", p->ieee_addr);
	printk("CRC checksum:         %04x\n", (u_short)(p->chksum));
	for (i=0; i<64; i++) {
	    printk("%3d %04x\n", i<<1, (u_short)*((u_short *)p+i));
	}
    }
}

static void
de4x5_dbg_rx(struct sk_buff *skb, int len)
{
    int i, j;

    if (de4x5_debug & DEBUG_RX) {
	printk("R: %pM <- %pM len/SAP:%02x%02x [%d]\n",
	       skb->data, &skb->data[6],
	       (u_char)skb->data[12],
	       (u_char)skb->data[13],
	       len);
	for (j=0; len>0;j+=16, len-=16) {
	  printk("    %03x: ",j);
	  for (i=0; i<16 && i<len; i++) {
	    printk("%02x ",(u_char)skb->data[i+j]);
	  }
	  printk("\n");
	}
    }
}

/*
** Perform IOCTL call functions here. Some are privileged operations and the
** effective uid is checked in those cases. In the normal course of events
** this function is only used for my testing.
*/
static int
de4x5_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
    struct de4x5_private *lp = netdev_priv(dev);
    struct de4x5_ioctl *ioc = (struct de4x5_ioctl *) &rq->ifr_ifru;
    u_long iobase = dev->base_addr;
    int i, j, status = 0;
    s32 omr;
    union {
	u8  addr[144];
	u16 sval[72];
	u32 lval[36];
    } tmp;
    u_long flags = 0;

    switch(ioc->cmd) {
    case DE4X5_GET_HWADDR:           /* Get the hardware address */
	ioc->len = ETH_ALEN;
	for (i=0; i<ETH_ALEN; i++) {
	    tmp.addr[i] = dev->dev_addr[i];
	}
	if (copy_to_user(ioc->data, tmp.addr, ioc->len)) return -EFAULT;
	break;

    case DE4X5_SET_HWADDR:           /* Set the hardware address */
	if (!capable(CAP_NET_ADMIN)) return -EPERM;
	if (copy_from_user(tmp.addr, ioc->data, ETH_ALEN)) return -EFAULT;
	if (netif_queue_stopped(dev))
		return -EBUSY;
	netif_stop_queue(dev);
	for (i=0; i<ETH_ALEN; i++) {
	    dev->dev_addr[i] = tmp.addr[i];
	}
	build_setup_frame(dev, PHYS_ADDR_ONLY);
	/* Set up the descriptor and give ownership to the card */
	load_packet(dev, lp->setup_frame, TD_IC | PERFECT_F | TD_SET |
		                                       SETUP_FRAME_LEN, (struct sk_buff *)1);
	lp->tx_new = (lp->tx_new + 1) % lp->txRingSize;
	outl(POLL_DEMAND, DE4X5_TPD);                /* Start the TX */
	netif_wake_queue(dev);                      /* Unlock the TX ring */
	break;

    case DE4X5_SAY_BOO:              /* Say "Boo!" to the kernel log file */
	if (!capable(CAP_NET_ADMIN)) return -EPERM;
	printk("%s: Boo!\n", dev->name);
	break;

    case DE4X5_MCA_EN:               /* Enable pass all multicast addressing */
	if (!capable(CAP_NET_ADMIN)) return -EPERM;
	omr = inl(DE4X5_OMR);
	omr |= OMR_PM;
	outl(omr, DE4X5_OMR);
	break;

    case DE4X5_GET_STATS:            /* Get the driver statistics */
    {
        struct pkt_stats statbuf;
	ioc->len = sizeof(statbuf);
	spin_lock_irqsave(&lp->lock, flags);
	memcpy(&statbuf, &lp->pktStats, ioc->len);
	spin_unlock_irqrestore(&lp->lock, flags);
	if (copy_to_user(ioc->data, &statbuf, ioc->len))
		return -EFAULT;
	break;
    }
    case DE4X5_CLR_STATS:            /* Zero out the driver statistics */
	if (!capable(CAP_NET_ADMIN)) return -EPERM;
	spin_lock_irqsave(&lp->lock, flags);
	memset(&lp->pktStats, 0, sizeof(lp->pktStats));
	spin_unlock_irqrestore(&lp->lock, flags);
	break;

    case DE4X5_GET_OMR:              /* Get the OMR Register contents */
	tmp.addr[0] = inl(DE4X5_OMR);
	if (copy_to_user(ioc->data, tmp.addr, 1)) return -EFAULT;
	break;

    case DE4X5_SET_OMR:              /* Set the OMR Register contents */
	if (!capable(CAP_NET_ADMIN)) return -EPERM;
	if (copy_from_user(tmp.addr, ioc->data, 1)) return -EFAULT;
	outl(tmp.addr[0], DE4X5_OMR);
	break;

    case DE4X5_GET_REG:              /* Get the DE4X5 Registers */
	j = 0;
	tmp.lval[0] = inl(DE4X5_STS); j+=4;
	tmp.lval[1] = inl(DE4X5_BMR); j+=4;
	tmp.lval[2] = inl(DE4X5_IMR); j+=4;
	tmp.lval[3] = inl(DE4X5_OMR); j+=4;
	tmp.lval[4] = inl(DE4X5_SISR); j+=4;
	tmp.lval[5] = inl(DE4X5_SICR); j+=4;
	tmp.lval[6] = inl(DE4X5_STRR); j+=4;
	tmp.lval[7] = inl(DE4X5_SIGR); j+=4;
	ioc->len = j;
	if (copy_to_user(ioc->data, tmp.lval, ioc->len))
		return -EFAULT;
	break;

#define DE4X5_DUMP              0x0f /* Dump the DE4X5 Status */
/*
      case DE4X5_DUMP:
	j = 0;
	tmp.addr[j++] = dev->irq;
	for (i=0; i<ETH_ALEN; i++) {
	    tmp.addr[j++] = dev->dev_addr[i];
	}
	tmp.addr[j++] = lp->rxRingSize;
	tmp.lval[j>>2] = (long)lp->rx_ring; j+=4;
	tmp.lval[j>>2] = (long)lp->tx_ring; j+=4;

	for (i=0;i<lp->rxRingSize-1;i++){
	    if (i < 3) {
		tmp.lval[j>>2] = (long)&lp->rx_ring[i].status; j+=4;
	    }
	}
	tmp.lval[j>>2] = (long)&lp->rx_ring[i].status; j+=4;
	for (i=0;i<lp->txRingSize-1;i++){
	    if (i < 3) {
		tmp.lval[j>>2] = (long)&lp->tx_ring[i].status; j+=4;
	    }
	}
	tmp.lval[j>>2] = (long)&lp->tx_ring[i].status; j+=4;

	for (i=0;i<lp->rxRingSize-1;i++){
	    if (i < 3) {
		tmp.lval[j>>2] = (s32)le32_to_cpu(lp->rx_ring[i].buf); j+=4;
	    }
	}
	tmp.lval[j>>2] = (s32)le32_to_cpu(lp->rx_ring[i].buf); j+=4;
	for (i=0;i<lp->txRingSize-1;i++){
	    if (i < 3) {
		tmp.lval[j>>2] = (s32)le32_to_cpu(lp->tx_ring[i].buf); j+=4;
	    }
	}
	tmp.lval[j>>2] = (s32)le32_to_cpu(lp->tx_ring[i].buf); j+=4;

	for (i=0;i<lp->rxRingSize;i++){
	    tmp.lval[j>>2] = le32_to_cpu(lp->rx_ring[i].status); j+=4;
	}
	for (i=0;i<lp->txRingSize;i++){
	    tmp.lval[j>>2] = le32_to_cpu(lp->tx_ring[i].status); j+=4;
	}

	tmp.lval[j>>2] = inl(DE4X5_BMR);  j+=4;
	tmp.lval[j>>2] = inl(DE4X5_TPD);  j+=4;
	tmp.lval[j>>2] = inl(DE4X5_RPD);  j+=4;
	tmp.lval[j>>2] = inl(DE4X5_RRBA); j+=4;
	tmp.lval[j>>2] = inl(DE4X5_TRBA); j+=4;
	tmp.lval[j>>2] = inl(DE4X5_STS);  j+=4;
	tmp.lval[j>>2] = inl(DE4X5_OMR);  j+=4;
	tmp.lval[j>>2] = inl(DE4X5_IMR);  j+=4;
	tmp.lval[j>>2] = lp->chipset; j+=4;
	if (lp->chipset == DC21140) {
	    tmp.lval[j>>2] = gep_rd(dev);  j+=4;
	} else {
	    tmp.lval[j>>2] = inl(DE4X5_SISR); j+=4;
	    tmp.lval[j>>2] = inl(DE4X5_SICR); j+=4;
	    tmp.lval[j>>2] = inl(DE4X5_STRR); j+=4;
	    tmp.lval[j>>2] = inl(DE4X5_SIGR); j+=4;
	}
	tmp.lval[j>>2] = lp->phy[lp->active].id; j+=4;
	if (lp->phy[lp->active].id && (!lp->useSROM || lp->useMII)) {
	    tmp.lval[j>>2] = lp->active; j+=4;
	    tmp.lval[j>>2]=mii_rd(MII_CR,lp->phy[lp->active].addr,DE4X5_MII); j+=4;
	    tmp.lval[j>>2]=mii_rd(MII_SR,lp->phy[lp->active].addr,DE4X5_MII); j+=4;
	    tmp.lval[j>>2]=mii_rd(MII_ID0,lp->phy[lp->active].addr,DE4X5_MII); j+=4;
	    tmp.lval[j>>2]=mii_rd(MII_ID1,lp->phy[lp->active].addr,DE4X5_MII); j+=4;
	    if (lp->phy[lp->active].id != BROADCOM_T4) {
		tmp.lval[j>>2]=mii_rd(MII_ANA,lp->phy[lp->active].addr,DE4X5_MII); j+=4;
		tmp.lval[j>>2]=mii_rd(MII_ANLPA,lp->phy[lp->active].addr,DE4X5_MII); j+=4;
	    }
	    tmp.lval[j>>2]=mii_rd(0x10,lp->phy[lp->active].addr,DE4X5_MII); j+=4;
	    if (lp->phy[lp->active].id != BROADCOM_T4) {
		tmp.lval[j>>2]=mii_rd(0x11,lp->phy[lp->active].addr,DE4X5_MII); j+=4;
		tmp.lval[j>>2]=mii_rd(0x12,lp->phy[lp->active].addr,DE4X5_MII); j+=4;
	    } else {
		tmp.lval[j>>2]=mii_rd(0x14,lp->phy[lp->active].addr,DE4X5_MII); j+=4;
	    }
	}

	tmp.addr[j++] = lp->txRingSize;
	tmp.addr[j++] = netif_queue_stopped(dev);

	ioc->len = j;
	if (copy_to_user(ioc->data, tmp.addr, ioc->len)) return -EFAULT;
	break;

*/
    default:
	return -EOPNOTSUPP;
    }

    return status;
}

static int __init de4x5_module_init (void)
{
	int err = 0;

#ifdef CONFIG_PCI
	err = pci_register_driver(&de4x5_pci_driver);
#endif
#ifdef CONFIG_EISA
	err |= eisa_driver_register (&de4x5_eisa_driver);
#endif

	return err;
}

static void __exit de4x5_module_exit (void)
{
#ifdef CONFIG_PCI
	pci_unregister_driver (&de4x5_pci_driver);
#endif
#ifdef CONFIG_EISA
	eisa_driver_unregister (&de4x5_eisa_driver);
#endif
}

module_init (de4x5_module_init);
module_exit (de4x5_module_exit);
