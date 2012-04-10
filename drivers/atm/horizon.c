/*
  Madge Horizon ATM Adapter driver.
  Copyright (C) 1995-1999  Madge Networks Ltd.
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
  
  The GNU GPL is contained in /usr/doc/copyright/GPL on a Debian
  system and in the file COPYING in the Linux kernel source.
*/

/*
  IMPORTANT NOTE: Madge Networks no longer makes the adapters
  supported by this driver and makes no commitment to maintain it.
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/atm.h>
#include <linux/atmdev.h>
#include <linux/sonet.h>
#include <linux/skbuff.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/uio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/wait.h>
#include <linux/slab.h>

#include <asm/io.h>
#include <linux/atomic.h>
#include <asm/uaccess.h>
#include <asm/string.h>
#include <asm/byteorder.h>

#include "horizon.h"

#define maintainer_string "Giuliano Procida at Madge Networks <gprocida@madge.com>"
#define description_string "Madge ATM Horizon [Ultra] driver"
#define version_string "1.2.1"

static inline void __init show_version (void) {
  printk ("%s version %s\n", description_string, version_string);
}

/*
  
  CREDITS
  
  Driver and documentation by:
  
  Chris Aston        Madge Networks
  Giuliano Procida   Madge Networks
  Simon Benham       Madge Networks
  Simon Johnson      Madge Networks
  Various Others     Madge Networks
  
  Some inspiration taken from other drivers by:
  
  Alexandru Cucos    UTBv
  Kari Mettinen      University of Helsinki
  Werner Almesberger EPFL LRC
  
  Theory of Operation
  
  I Hardware, detection, initialisation and shutdown.
  
  1. Supported Hardware
  
  This driver should handle all variants of the PCI Madge ATM adapters
  with the Horizon chipset. These are all PCI cards supporting PIO, BM
  DMA and a form of MMIO (registers only, not internal RAM).
  
  The driver is only known to work with SONET and UTP Horizon Ultra
  cards at 155Mb/s. However, code is in place to deal with both the
  original Horizon and 25Mb/s operation.
  
  There are two revisions of the Horizon ASIC: the original and the
  Ultra. Details of hardware bugs are in section III.
  
  The ASIC version can be distinguished by chip markings but is NOT
  indicated by the PCI revision (all adapters seem to have PCI rev 1).
  
  I believe that:
  
  Horizon       => Collage  25 PCI Adapter (UTP and STP)
  Horizon Ultra => Collage 155 PCI Client (UTP or SONET)
  Ambassador x  => Collage 155 PCI Server (completely different)
  
  Horizon (25Mb/s) is fitted with UTP and STP connectors. It seems to
  have a Madge B154 plus glue logic serializer. I have also found a
  really ancient version of this with slightly different glue. It
  comes with the revision 0 (140-025-01) ASIC.
  
  Horizon Ultra (155Mb/s) is fitted with either a Pulse Medialink
  output (UTP) or an HP HFBR 5205 output (SONET). It has either
  Madge's SAMBA framer or a SUNI-lite device (early versions). It
  comes with the revision 1 (140-027-01) ASIC.
  
  2. Detection
  
  All Horizon-based cards present with the same PCI Vendor and Device
  IDs. The standard Linux 2.2 PCI API is used to locate any cards and
  to enable bus-mastering (with appropriate latency).
  
  ATM_LAYER_STATUS in the control register distinguishes between the
  two possible physical layers (25 and 155). It is not clear whether
  the 155 cards can also operate at 25Mbps. We rely on the fact that a
  card operates at 155 if and only if it has the newer Horizon Ultra
  ASIC.
  
  For 155 cards the two possible framers are probed for and then set
  up for loop-timing.
  
  3. Initialisation
  
  The card is reset and then put into a known state. The physical
  layer is configured for normal operation at the appropriate speed;
  in the case of the 155 cards, the framer is initialised with
  line-based timing; the internal RAM is zeroed and the allocation of
  buffers for RX and TX is made; the Burnt In Address is read and
  copied to the ATM ESI; various policy settings for RX (VPI bits,
  unknown VCs, oam cells) are made. Ideally all policy items should be
  configurable at module load (if not actually on-demand), however,
  only the vpi vs vci bit allocation can be specified at insmod.
  
  4. Shutdown
  
  This is in response to module_cleaup. No VCs are in use and the card
  should be idle; it is reset.
  
  II Driver software (as it should be)
  
  0. Traffic Parameters
  
  The traffic classes (not an enumeration) are currently: ATM_NONE (no
  traffic), ATM_UBR, ATM_CBR, ATM_VBR and ATM_ABR, ATM_ANYCLASS
  (compatible with everything). Together with (perhaps only some of)
  the following items they make up the traffic specification.
  
  struct atm_trafprm {
    unsigned char traffic_class; traffic class (ATM_UBR, ...)
    int           max_pcr;       maximum PCR in cells per second
    int           pcr;           desired PCR in cells per second
    int           min_pcr;       minimum PCR in cells per second
    int           max_cdv;       maximum CDV in microseconds
    int           max_sdu;       maximum SDU in bytes
  };
  
  Note that these denote bandwidth available not bandwidth used; the
  possibilities according to ATMF are:
  
  Real Time (cdv and max CDT given)
  
  CBR(pcr)             pcr bandwidth always available
  rtVBR(pcr,scr,mbs)   scr bandwidth always available, up to pcr at mbs too
  
  Non Real Time
  
  nrtVBR(pcr,scr,mbs)  scr bandwidth always available, up to pcr at mbs too
  UBR()
  ABR(mcr,pcr)         mcr bandwidth always available, up to pcr (depending) too
  
  mbs is max burst size (bucket)
  pcr and scr have associated cdvt values
  mcr is like scr but has no cdtv
  cdtv may differ at each hop
  
  Some of the above items are qos items (as opposed to traffic
  parameters). We have nothing to do with qos. All except ABR can have
  their traffic parameters converted to GCRA parameters. The GCRA may
  be implemented as a (real-number) leaky bucket. The GCRA can be used
  in complicated ways by switches and in simpler ways by end-stations.
  It can be used both to filter incoming cells and shape out-going
  cells.
  
  ATM Linux actually supports:
  
  ATM_NONE() (no traffic in this direction)
  ATM_UBR(max_frame_size)
  ATM_CBR(max/min_pcr, max_cdv, max_frame_size)
  
  0 or ATM_MAX_PCR are used to indicate maximum available PCR
  
  A traffic specification consists of the AAL type and separate
  traffic specifications for either direction. In ATM Linux it is:
  
  struct atm_qos {
  struct atm_trafprm txtp;
  struct atm_trafprm rxtp;
  unsigned char aal;
  };
  
  AAL types are:
  
  ATM_NO_AAL    AAL not specified
  ATM_AAL0      "raw" ATM cells
  ATM_AAL1      AAL1 (CBR)
  ATM_AAL2      AAL2 (VBR)
  ATM_AAL34     AAL3/4 (data)
  ATM_AAL5      AAL5 (data)
  ATM_SAAL      signaling AAL
  
  The Horizon has support for AAL frame types: 0, 3/4 and 5. However,
  it does not implement AAL 3/4 SAR and it has a different notion of
  "raw cell" to ATM Linux's (48 bytes vs. 52 bytes) so neither are
  supported by this driver.
  
  The Horizon has limited support for ABR (including UBR), VBR and
  CBR. Each TX channel has a bucket (containing up to 31 cell units)
  and two timers (PCR and SCR) associated with it that can be used to
  govern cell emissions and host notification (in the case of ABR this
  is presumably so that RM cells may be emitted at appropriate times).
  The timers may either be disabled or may be set to any of 240 values
  (determined by the clock crystal, a fixed (?) per-device divider, a
  configurable divider and a configurable timer preload value).
  
  At the moment only UBR and CBR are supported by the driver. VBR will
  be supported as soon as ATM for Linux supports it. ABR support is
  very unlikely as RM cell handling is completely up to the driver.
  
  1. TX (TX channel setup and TX transfer)
  
  The TX half of the driver owns the TX Horizon registers. The TX
  component in the IRQ handler is the BM completion handler. This can
  only be entered when tx_busy is true (enforced by hardware). The
  other TX component can only be entered when tx_busy is false
  (enforced by driver). So TX is single-threaded.
  
  Apart from a minor optimisation to not re-select the last channel,
  the TX send component works as follows:
  
  Atomic test and set tx_busy until we succeed; we should implement
  some sort of timeout so that tx_busy will never be stuck at true.
  
  If no TX channel is set up for this VC we wait for an idle one (if
  necessary) and set it up.
  
  At this point we have a TX channel ready for use. We wait for enough
  buffers to become available then start a TX transmit (set the TX
  descriptor, schedule transfer, exit).
  
  The IRQ component handles TX completion (stats, free buffer, tx_busy
  unset, exit). We also re-schedule further transfers for the same
  frame if needed.
  
  TX setup in more detail:
  
  TX open is a nop, the relevant information is held in the hrz_vcc
  (vcc->dev_data) structure and is "cached" on the card.
  
  TX close gets the TX lock and clears the channel from the "cache".
  
  2. RX (Data Available and RX transfer)
  
  The RX half of the driver owns the RX registers. There are two RX
  components in the IRQ handler: the data available handler deals with
  fresh data that has arrived on the card, the BM completion handler
  is very similar to the TX completion handler. The data available
  handler grabs the rx_lock and it is only released once the data has
  been discarded or completely transferred to the host. The BM
  completion handler only runs when the lock is held; the data
  available handler is locked out over the same period.
  
  Data available on the card triggers an interrupt. If the data is not
  suitable for our existing RX channels or we cannot allocate a buffer
  it is flushed. Otherwise an RX receive is scheduled. Multiple RX
  transfers may be scheduled for the same frame.
  
  RX setup in more detail:
  
  RX open...
  RX close...
  
  III Hardware Bugs
  
  0. Byte vs Word addressing of adapter RAM.
  
  A design feature; see the .h file (especially the memory map).
  
  1. Bus Master Data Transfers (original Horizon only, fixed in Ultra)
  
  The host must not start a transmit direction transfer at a
  non-four-byte boundary in host memory. Instead the host should
  perform a byte, or a two byte, or one byte followed by two byte
  transfer in order to start the rest of the transfer on a four byte
  boundary. RX is OK.
  
  Simultaneous transmit and receive direction bus master transfers are
  not allowed.
  
  The simplest solution to these two is to always do PIO (never DMA)
  in the TX direction on the original Horizon. More complicated
  solutions are likely to hurt my brain.
  
  2. Loss of buffer on close VC
  
  When a VC is being closed, the buffer associated with it is not
  returned to the pool. The host must store the reference to this
  buffer and when opening a new VC then give it to that new VC.
  
  The host intervention currently consists of stacking such a buffer
  pointer at VC close and checking the stack at VC open.
  
  3. Failure to close a VC
  
  If a VC is currently receiving a frame then closing the VC may fail
  and the frame continues to be received.
  
  The solution is to make sure any received frames are flushed when
  ready. This is currently done just before the solution to 2.
  
  4. PCI bus (original Horizon only, fixed in Ultra)
  
  Reading from the data port prior to initialisation will hang the PCI
  bus. Just don't do that then! We don't.
  
  IV To Do List
  
  . Timer code may be broken.
  
  . Allow users to specify buffer allocation split for TX and RX.
  
  . Deal once and for all with buggy VC close.
  
  . Handle interrupted and/or non-blocking operations.
  
  . Change some macros to functions and move from .h to .c.
  
  . Try to limit the number of TX frames each VC may have queued, in
    order to reduce the chances of TX buffer exhaustion.
  
  . Implement VBR (bucket and timers not understood) and ABR (need to
    do RM cells manually); also no Linux support for either.
  
  . Implement QoS changes on open VCs (involves extracting parts of VC open
    and close into separate functions and using them to make changes).
  
*/

/********** globals **********/

static void do_housekeeping (unsigned long arg);

static unsigned short debug = 0;
static unsigned short vpi_bits = 0;
static int max_tx_size = 9000;
static int max_rx_size = 9000;
static unsigned char pci_lat = 0;

/********** access functions **********/

/* Read / Write Horizon registers */
static inline void wr_regl (const hrz_dev * dev, unsigned char reg, u32 data) {
  outl (cpu_to_le32 (data), dev->iobase + reg);
}

static inline u32 rd_regl (const hrz_dev * dev, unsigned char reg) {
  return le32_to_cpu (inl (dev->iobase + reg));
}

static inline void wr_regw (const hrz_dev * dev, unsigned char reg, u16 data) {
  outw (cpu_to_le16 (data), dev->iobase + reg);
}

static inline u16 rd_regw (const hrz_dev * dev, unsigned char reg) {
  return le16_to_cpu (inw (dev->iobase + reg));
}

static inline void wrs_regb (const hrz_dev * dev, unsigned char reg, void * addr, u32 len) {
  outsb (dev->iobase + reg, addr, len);
}

static inline void rds_regb (const hrz_dev * dev, unsigned char reg, void * addr, u32 len) {
  insb (dev->iobase + reg, addr, len);
}

/* Read / Write to a given address in Horizon buffer memory.
   Interrupts must be disabled between the address register and data
   port accesses as these must form an atomic operation. */
static inline void wr_mem (const hrz_dev * dev, HDW * addr, u32 data) {
  // wr_regl (dev, MEM_WR_ADDR_REG_OFF, (u32) addr);
  wr_regl (dev, MEM_WR_ADDR_REG_OFF, (addr - (HDW *) 0) * sizeof(HDW));
  wr_regl (dev, MEMORY_PORT_OFF, data);
}

static inline u32 rd_mem (const hrz_dev * dev, HDW * addr) {
  // wr_regl (dev, MEM_RD_ADDR_REG_OFF, (u32) addr);
  wr_regl (dev, MEM_RD_ADDR_REG_OFF, (addr - (HDW *) 0) * sizeof(HDW));
  return rd_regl (dev, MEMORY_PORT_OFF);
}

static inline void wr_framer (const hrz_dev * dev, u32 addr, u32 data) {
  wr_regl (dev, MEM_WR_ADDR_REG_OFF, (u32) addr | 0x80000000);
  wr_regl (dev, MEMORY_PORT_OFF, data);
}

static inline u32 rd_framer (const hrz_dev * dev, u32 addr) {
  wr_regl (dev, MEM_RD_ADDR_REG_OFF, (u32) addr | 0x80000000);
  return rd_regl (dev, MEMORY_PORT_OFF);
}

/********** specialised access functions **********/

/* RX */

static inline void FLUSH_RX_CHANNEL (hrz_dev * dev, u16 channel) {
  wr_regw (dev, RX_CHANNEL_PORT_OFF, FLUSH_CHANNEL | channel);
  return;
}

static void WAIT_FLUSH_RX_COMPLETE (hrz_dev * dev) {
  while (rd_regw (dev, RX_CHANNEL_PORT_OFF) & FLUSH_CHANNEL)
    ;
  return;
}

static inline void SELECT_RX_CHANNEL (hrz_dev * dev, u16 channel) {
  wr_regw (dev, RX_CHANNEL_PORT_OFF, channel);
  return;
}

static void WAIT_UPDATE_COMPLETE (hrz_dev * dev) {
  while (rd_regw (dev, RX_CHANNEL_PORT_OFF) & RX_CHANNEL_UPDATE_IN_PROGRESS)
    ;
  return;
}

/* TX */

static inline void SELECT_TX_CHANNEL (hrz_dev * dev, u16 tx_channel) {
  wr_regl (dev, TX_CHANNEL_PORT_OFF, tx_channel);
  return;
}

/* Update or query one configuration parameter of a particular channel. */

static inline void update_tx_channel_config (hrz_dev * dev, short chan, u8 mode, u16 value) {
  wr_regw (dev, TX_CHANNEL_CONFIG_COMMAND_OFF,
	   chan * TX_CHANNEL_CONFIG_MULT | mode);
    wr_regw (dev, TX_CHANNEL_CONFIG_DATA_OFF, value);
    return;
}

static inline u16 query_tx_channel_config (hrz_dev * dev, short chan, u8 mode) {
  wr_regw (dev, TX_CHANNEL_CONFIG_COMMAND_OFF,
	   chan * TX_CHANNEL_CONFIG_MULT | mode);
    return rd_regw (dev, TX_CHANNEL_CONFIG_DATA_OFF);
}

/********** dump functions **********/

static inline void dump_skb (char * prefix, unsigned int vc, struct sk_buff * skb) {
#ifdef DEBUG_HORIZON
  unsigned int i;
  unsigned char * data = skb->data;
  PRINTDB (DBG_DATA, "%s(%u) ", prefix, vc);
  for (i=0; i<skb->len && i < 256;i++)
    PRINTDM (DBG_DATA, "%02x ", data[i]);
  PRINTDE (DBG_DATA,"");
#else
  (void) prefix;
  (void) vc;
  (void) skb;
#endif
  return;
}

static inline void dump_regs (hrz_dev * dev) {
#ifdef DEBUG_HORIZON
  PRINTD (DBG_REGS, "CONTROL 0: %#x", rd_regl (dev, CONTROL_0_REG));
  PRINTD (DBG_REGS, "RX CONFIG: %#x", rd_regw (dev, RX_CONFIG_OFF));
  PRINTD (DBG_REGS, "TX CONFIG: %#x", rd_regw (dev, TX_CONFIG_OFF));
  PRINTD (DBG_REGS, "TX STATUS: %#x", rd_regw (dev, TX_STATUS_OFF));
  PRINTD (DBG_REGS, "IRQ ENBLE: %#x", rd_regl (dev, INT_ENABLE_REG_OFF));
  PRINTD (DBG_REGS, "IRQ SORCE: %#x", rd_regl (dev, INT_SOURCE_REG_OFF));
#else
  (void) dev;
#endif
  return;
}

static inline void dump_framer (hrz_dev * dev) {
#ifdef DEBUG_HORIZON
  unsigned int i;
  PRINTDB (DBG_REGS, "framer registers:");
  for (i = 0; i < 0x10; ++i)
    PRINTDM (DBG_REGS, " %02x", rd_framer (dev, i));
  PRINTDE (DBG_REGS,"");
#else
  (void) dev;
#endif
  return;
}

/********** VPI/VCI <-> (RX) channel conversions **********/

/* RX channels are 10 bit integers, these fns are quite paranoid */

static inline int channel_to_vpivci (const u16 channel, short * vpi, int * vci) {
  unsigned short vci_bits = 10 - vpi_bits;
  if ((channel & RX_CHANNEL_MASK) == channel) {
    *vci = channel & ((~0)<<vci_bits);
    *vpi = channel >> vci_bits;
    return channel ? 0 : -EINVAL;
  }
  return -EINVAL;
}

static inline int vpivci_to_channel (u16 * channel, const short vpi, const int vci) {
  unsigned short vci_bits = 10 - vpi_bits;
  if (0 <= vpi && vpi < 1<<vpi_bits && 0 <= vci && vci < 1<<vci_bits) {
    *channel = vpi<<vci_bits | vci;
    return *channel ? 0 : -EINVAL;
  }
  return -EINVAL;
}

/********** decode RX queue entries **********/

static inline u16 rx_q_entry_to_length (u32 x) {
  return x & RX_Q_ENTRY_LENGTH_MASK;
}

static inline u16 rx_q_entry_to_rx_channel (u32 x) {
  return (x>>RX_Q_ENTRY_CHANNEL_SHIFT) & RX_CHANNEL_MASK;
}

/* Cell Transmit Rate Values
 *
 * the cell transmit rate (cells per sec) can be set to a variety of
 * different values by specifying two parameters: a timer preload from
 * 1 to 16 (stored as 0 to 15) and a clock divider (2 to the power of
 * an exponent from 0 to 14; the special value 15 disables the timer).
 *
 * cellrate = baserate / (preload * 2^divider)
 *
 * The maximum cell rate that can be specified is therefore just the
 * base rate. Halving the preload is equivalent to adding 1 to the
 * divider and so values 1 to 8 of the preload are redundant except
 * in the case of a maximal divider (14).
 *
 * Given a desired cell rate, an algorithm to determine the preload
 * and divider is:
 * 
 * a) x = baserate / cellrate, want p * 2^d = x (as far as possible)
 * b) if x > 16 * 2^14 then set p = 16, d = 14 (min rate), done
 *    if x <= 16 then set p = x, d = 0 (high rates), done
 * c) now have 16 < x <= 2^18, or 1 < x/16 <= 2^14 and we want to
 *    know n such that 2^(n-1) < x/16 <= 2^n, so slide a bit until
 *    we find the range (n will be between 1 and 14), set d = n
 * d) Also have 8 < x/2^n <= 16, so set p nearest x/2^n
 *
 * The algorithm used below is a minor variant of the above.
 *
 * The base rate is derived from the oscillator frequency (Hz) using a
 * fixed divider:
 *
 * baserate = freq / 32 in the case of some Unknown Card
 * baserate = freq / 8  in the case of the Horizon        25
 * baserate = freq / 8  in the case of the Horizon Ultra 155
 *
 * The Horizon cards have oscillators and base rates as follows:
 *
 * Card               Oscillator  Base Rate
 * Unknown Card       33 MHz      1.03125 MHz (33 MHz = PCI freq)
 * Horizon        25  32 MHz      4       MHz
 * Horizon Ultra 155  40 MHz      5       MHz
 *
 * The following defines give the base rates in Hz. These were
 * previously a factor of 100 larger, no doubt someone was using
 * cps*100.
 */

#define BR_UKN 1031250l
#define BR_HRZ 4000000l
#define BR_ULT 5000000l

// d is an exponent
#define CR_MIND 0
#define CR_MAXD 14

// p ranges from 1 to a power of 2
#define CR_MAXPEXP 4
 
static int make_rate (const hrz_dev * dev, u32 c, rounding r,
		      u16 * bits, unsigned int * actual)
{
	// note: rounding the rate down means rounding 'p' up
	const unsigned long br = test_bit(ultra, &dev->flags) ? BR_ULT : BR_HRZ;
  
	u32 div = CR_MIND;
	u32 pre;
  
	// br_exp and br_man are used to avoid overflowing (c*maxp*2^d) in
	// the tests below. We could think harder about exact possibilities
	// of failure...
  
	unsigned long br_man = br;
	unsigned int br_exp = 0;
  
	PRINTD (DBG_QOS|DBG_FLOW, "make_rate b=%lu, c=%u, %s", br, c,
		r == round_up ? "up" : r == round_down ? "down" : "nearest");
  
	// avoid div by zero
	if (!c) {
		PRINTD (DBG_QOS|DBG_ERR, "zero rate is not allowed!");
		return -EINVAL;
	}
  
	while (br_exp < CR_MAXPEXP + CR_MIND && (br_man % 2 == 0)) {
		br_man = br_man >> 1;
		++br_exp;
	}
	// (br >>br_exp) <<br_exp == br and
	// br_exp <= CR_MAXPEXP+CR_MIND
  
	if (br_man <= (c << (CR_MAXPEXP+CR_MIND-br_exp))) {
		// Equivalent to: B <= (c << (MAXPEXP+MIND))
		// take care of rounding
		switch (r) {
			case round_down:
				pre = DIV_ROUND_UP(br, c<<div);
				// but p must be non-zero
				if (!pre)
					pre = 1;
				break;
			case round_nearest:
				pre = DIV_ROUND_CLOSEST(br, c<<div);
				// but p must be non-zero
				if (!pre)
					pre = 1;
				break;
			default:	/* round_up */
				pre = br/(c<<div);
				// but p must be non-zero
				if (!pre)
					return -EINVAL;
		}
		PRINTD (DBG_QOS, "A: p=%u, d=%u", pre, div);
		goto got_it;
	}
  
	// at this point we have
	// d == MIND and (c << (MAXPEXP+MIND)) < B
	while (div < CR_MAXD) {
		div++;
		if (br_man <= (c << (CR_MAXPEXP+div-br_exp))) {
			// Equivalent to: B <= (c << (MAXPEXP+d))
			// c << (MAXPEXP+d-1) < B <= c << (MAXPEXP+d)
			// 1 << (MAXPEXP-1) < B/2^d/c <= 1 << MAXPEXP
			// MAXP/2 < B/c2^d <= MAXP
			// take care of rounding
			switch (r) {
				case round_down:
					pre = DIV_ROUND_UP(br, c<<div);
					break;
				case round_nearest:
					pre = DIV_ROUND_CLOSEST(br, c<<div);
					break;
				default: /* round_up */
					pre = br/(c<<div);
			}
			PRINTD (DBG_QOS, "B: p=%u, d=%u", pre, div);
			goto got_it;
		}
	}
	// at this point we have
	// d == MAXD and (c << (MAXPEXP+MAXD)) < B
	// but we cannot go any higher
	// take care of rounding
	if (r == round_down)
		return -EINVAL;
	pre = 1 << CR_MAXPEXP;
	PRINTD (DBG_QOS, "C: p=%u, d=%u", pre, div);
got_it:
	// paranoia
	if (div > CR_MAXD || (!pre) || pre > 1<<CR_MAXPEXP) {
		PRINTD (DBG_QOS, "set_cr internal failure: d=%u p=%u",
			div, pre);
		return -EINVAL;
	} else {
		if (bits)
			*bits = (div<<CLOCK_SELECT_SHIFT) | (pre-1);
		if (actual) {
			*actual = DIV_ROUND_UP(br, pre<<div);
			PRINTD (DBG_QOS, "actual rate: %u", *actual);
		}
		return 0;
	}
}

static int make_rate_with_tolerance (const hrz_dev * dev, u32 c, rounding r, unsigned int tol,
				     u16 * bit_pattern, unsigned int * actual) {
  unsigned int my_actual;
  
  PRINTD (DBG_QOS|DBG_FLOW, "make_rate_with_tolerance c=%u, %s, tol=%u",
	  c, (r == round_up) ? "up" : (r == round_down) ? "down" : "nearest", tol);
  
  if (!actual)
    // actual rate is not returned
    actual = &my_actual;
  
  if (make_rate (dev, c, round_nearest, bit_pattern, actual))
    // should never happen as round_nearest always succeeds
    return -1;
  
  if (c - tol <= *actual && *actual <= c + tol)
    // within tolerance
    return 0;
  else
    // intolerant, try rounding instead
    return make_rate (dev, c, r, bit_pattern, actual);
}

/********** Listen on a VC **********/

static int hrz_open_rx (hrz_dev * dev, u16 channel) {
  // is there any guarantee that we don't get two simulataneous
  // identical calls of this function from different processes? yes
  // rate_lock
  unsigned long flags;
  u32 channel_type; // u16?
  
  u16 buf_ptr = RX_CHANNEL_IDLE;
  
  rx_ch_desc * rx_desc = &memmap->rx_descs[channel];
  
  PRINTD (DBG_FLOW, "hrz_open_rx %x", channel);
  
  spin_lock_irqsave (&dev->mem_lock, flags);
  channel_type = rd_mem (dev, &rx_desc->wr_buf_type) & BUFFER_PTR_MASK;
  spin_unlock_irqrestore (&dev->mem_lock, flags);
  
  // very serious error, should never occur
  if (channel_type != RX_CHANNEL_DISABLED) {
    PRINTD (DBG_ERR|DBG_VCC, "RX channel for VC already open");
    return -EBUSY; // clean up?
  }
  
  // Give back spare buffer
  if (dev->noof_spare_buffers) {
    buf_ptr = dev->spare_buffers[--dev->noof_spare_buffers];
    PRINTD (DBG_VCC, "using a spare buffer: %u", buf_ptr);
    // should never occur
    if (buf_ptr == RX_CHANNEL_DISABLED || buf_ptr == RX_CHANNEL_IDLE) {
      // but easy to recover from
      PRINTD (DBG_ERR|DBG_VCC, "bad spare buffer pointer, using IDLE");
      buf_ptr = RX_CHANNEL_IDLE;
    }
  } else {
    PRINTD (DBG_VCC, "using IDLE buffer pointer");
  }
  
  // Channel is currently disabled so change its status to idle
  
  // do we really need to save the flags again?
  spin_lock_irqsave (&dev->mem_lock, flags);
  
  wr_mem (dev, &rx_desc->wr_buf_type,
	  buf_ptr | CHANNEL_TYPE_AAL5 | FIRST_CELL_OF_AAL5_FRAME);
  if (buf_ptr != RX_CHANNEL_IDLE)
    wr_mem (dev, &rx_desc->rd_buf_type, buf_ptr);
  
  spin_unlock_irqrestore (&dev->mem_lock, flags);
  
  // rxer->rate = make_rate (qos->peak_cells);
  
  PRINTD (DBG_FLOW, "hrz_open_rx ok");
  
  return 0;
}

#if 0
/********** change vc rate for a given vc **********/

static void hrz_change_vc_qos (ATM_RXER * rxer, MAAL_QOS * qos) {
  rxer->rate = make_rate (qos->peak_cells);
}
#endif

/********** free an skb (as per ATM device driver documentation) **********/

static void hrz_kfree_skb (struct sk_buff * skb) {
  if (ATM_SKB(skb)->vcc->pop) {
    ATM_SKB(skb)->vcc->pop (ATM_SKB(skb)->vcc, skb);
  } else {
    dev_kfree_skb_any (skb);
  }
}

/********** cancel listen on a VC **********/

static void hrz_close_rx (hrz_dev * dev, u16 vc) {
  unsigned long flags;
  
  u32 value;
  
  u32 r1, r2;
  
  rx_ch_desc * rx_desc = &memmap->rx_descs[vc];
  
  int was_idle = 0;
  
  spin_lock_irqsave (&dev->mem_lock, flags);
  value = rd_mem (dev, &rx_desc->wr_buf_type) & BUFFER_PTR_MASK;
  spin_unlock_irqrestore (&dev->mem_lock, flags);
  
  if (value == RX_CHANNEL_DISABLED) {
    // I suppose this could happen once we deal with _NONE traffic properly
    PRINTD (DBG_VCC, "closing VC: RX channel %u already disabled", vc);
    return;
  }
  if (value == RX_CHANNEL_IDLE)
    was_idle = 1;
  
  spin_lock_irqsave (&dev->mem_lock, flags);
  
  for (;;) {
    wr_mem (dev, &rx_desc->wr_buf_type, RX_CHANNEL_DISABLED);
    
    if ((rd_mem (dev, &rx_desc->wr_buf_type) & BUFFER_PTR_MASK) == RX_CHANNEL_DISABLED)
      break;
    
    was_idle = 0;
  }
  
  if (was_idle) {
    spin_unlock_irqrestore (&dev->mem_lock, flags);
    return;
  }
  
  WAIT_FLUSH_RX_COMPLETE(dev);
  
  // XXX Is this all really necessary? We can rely on the rx_data_av
  // handler to discard frames that remain queued for delivery. If the
  // worry is that immediately reopening the channel (perhaps by a
  // different process) may cause some data to be mis-delivered then
  // there may still be a simpler solution (such as busy-waiting on
  // rx_busy once the channel is disabled or before a new one is
  // opened - does this leave any holes?). Arguably setting up and
  // tearing down the TX and RX halves of each virtual circuit could
  // most safely be done within ?x_busy protected regions.
  
  // OK, current changes are that Simon's marker is disabled and we DO
  // look for NULL rxer elsewhere. The code here seems flush frames
  // and then remember the last dead cell belonging to the channel
  // just disabled - the cell gets relinked at the next vc_open.
  // However, when all VCs are closed or only a few opened there are a
  // handful of buffers that are unusable.
  
  // Does anyone feel like documenting spare_buffers properly?
  // Does anyone feel like fixing this in a nicer way?
  
  // Flush any data which is left in the channel
  for (;;) {
    // Change the rx channel port to something different to the RX
    // channel we are trying to close to force Horizon to flush the rx
    // channel read and write pointers.
    
    u16 other = vc^(RX_CHANS/2);
    
    SELECT_RX_CHANNEL (dev, other);
    WAIT_UPDATE_COMPLETE (dev);
    
    r1 = rd_mem (dev, &rx_desc->rd_buf_type);
    
    // Select this RX channel. Flush doesn't seem to work unless we
    // select an RX channel before hand
    
    SELECT_RX_CHANNEL (dev, vc);
    WAIT_UPDATE_COMPLETE (dev);
    
    // Attempt to flush a frame on this RX channel
    
    FLUSH_RX_CHANNEL (dev, vc);
    WAIT_FLUSH_RX_COMPLETE (dev);
    
    // Force Horizon to flush rx channel read and write pointers as before
    
    SELECT_RX_CHANNEL (dev, other);
    WAIT_UPDATE_COMPLETE (dev);
    
    r2 = rd_mem (dev, &rx_desc->rd_buf_type);
    
    PRINTD (DBG_VCC|DBG_RX, "r1 = %u, r2 = %u", r1, r2);
    
    if (r1 == r2) {
      dev->spare_buffers[dev->noof_spare_buffers++] = (u16)r1;
      break;
    }
  }
  
#if 0
  {
    rx_q_entry * wr_ptr = &memmap->rx_q_entries[rd_regw (dev, RX_QUEUE_WR_PTR_OFF)];
    rx_q_entry * rd_ptr = dev->rx_q_entry;
    
    PRINTD (DBG_VCC|DBG_RX, "rd_ptr = %u, wr_ptr = %u", rd_ptr, wr_ptr);
    
    while (rd_ptr != wr_ptr) {
      u32 x = rd_mem (dev, (HDW *) rd_ptr);
      
      if (vc == rx_q_entry_to_rx_channel (x)) {
	x |= SIMONS_DODGEY_MARKER;
	
	PRINTD (DBG_RX|DBG_VCC|DBG_WARN, "marking a frame as dodgey");
	
	wr_mem (dev, (HDW *) rd_ptr, x);
      }
      
      if (rd_ptr == dev->rx_q_wrap)
	rd_ptr = dev->rx_q_reset;
      else
	rd_ptr++;
    }
  }
#endif
  
  spin_unlock_irqrestore (&dev->mem_lock, flags);
  
  return;
}

/********** schedule RX transfers **********/

// Note on tail recursion: a GCC developer said that it is not likely
// to be fixed soon, so do not define TAILRECUSRIONWORKS unless you
// are sure it does as you may otherwise overflow the kernel stack.

// giving this fn a return value would help GCC, allegedly

static void rx_schedule (hrz_dev * dev, int irq) {
  unsigned int rx_bytes;
  
  int pio_instead = 0;
#ifndef TAILRECURSIONWORKS
  pio_instead = 1;
  while (pio_instead) {
#endif
    // bytes waiting for RX transfer
    rx_bytes = dev->rx_bytes;
    
#if 0
    spin_count = 0;
    while (rd_regl (dev, MASTER_RX_COUNT_REG_OFF)) {
      PRINTD (DBG_RX|DBG_WARN, "RX error: other PCI Bus Master RX still in progress!");
      if (++spin_count > 10) {
	PRINTD (DBG_RX|DBG_ERR, "spun out waiting PCI Bus Master RX completion");
	wr_regl (dev, MASTER_RX_COUNT_REG_OFF, 0);
	clear_bit (rx_busy, &dev->flags);
	hrz_kfree_skb (dev->rx_skb);
	return;
      }
    }
#endif
    
    // this code follows the TX code but (at the moment) there is only
    // one region - the skb itself. I don't know if this will change,
    // but it doesn't hurt to have the code here, disabled.
    
    if (rx_bytes) {
      // start next transfer within same region
      if (rx_bytes <= MAX_PIO_COUNT) {
	PRINTD (DBG_RX|DBG_BUS, "(pio)");
	pio_instead = 1;
      }
      if (rx_bytes <= MAX_TRANSFER_COUNT) {
	PRINTD (DBG_RX|DBG_BUS, "(simple or last multi)");
	dev->rx_bytes = 0;
      } else {
	PRINTD (DBG_RX|DBG_BUS, "(continuing multi)");
	dev->rx_bytes = rx_bytes - MAX_TRANSFER_COUNT;
	rx_bytes = MAX_TRANSFER_COUNT;
      }
    } else {
      // rx_bytes == 0 -- we're between regions
      // regions remaining to transfer
#if 0
      unsigned int rx_regions = dev->rx_regions;
#else
      unsigned int rx_regions = 0;
#endif
      
      if (rx_regions) {
#if 0
	// start a new region
	dev->rx_addr = dev->rx_iovec->iov_base;
	rx_bytes = dev->rx_iovec->iov_len;
	++dev->rx_iovec;
	dev->rx_regions = rx_regions - 1;
	
	if (rx_bytes <= MAX_PIO_COUNT) {
	  PRINTD (DBG_RX|DBG_BUS, "(pio)");
	  pio_instead = 1;
	}
	if (rx_bytes <= MAX_TRANSFER_COUNT) {
	  PRINTD (DBG_RX|DBG_BUS, "(full region)");
	  dev->rx_bytes = 0;
	} else {
	  PRINTD (DBG_RX|DBG_BUS, "(start multi region)");
	  dev->rx_bytes = rx_bytes - MAX_TRANSFER_COUNT;
	  rx_bytes = MAX_TRANSFER_COUNT;
	}
#endif
      } else {
	// rx_regions == 0
	// that's all folks - end of frame
	struct sk_buff * skb = dev->rx_skb;
	// dev->rx_iovec = 0;
	
	FLUSH_RX_CHANNEL (dev, dev->rx_channel);
	
	dump_skb ("<<<", dev->rx_channel, skb);
	
	PRINTD (DBG_RX|DBG_SKB, "push %p %u", skb->data, skb->len);
	
	{
	  struct atm_vcc * vcc = ATM_SKB(skb)->vcc;
	  // VC layer stats
	  atomic_inc(&vcc->stats->rx);
	  __net_timestamp(skb);
	  // end of our responsibility
	  vcc->push (vcc, skb);
	}
      }
    }
    
    // note: writing RX_COUNT clears any interrupt condition
    if (rx_bytes) {
      if (pio_instead) {
	if (irq)
	  wr_regl (dev, MASTER_RX_COUNT_REG_OFF, 0);
	rds_regb (dev, DATA_PORT_OFF, dev->rx_addr, rx_bytes);
      } else {
	wr_regl (dev, MASTER_RX_ADDR_REG_OFF, virt_to_bus (dev->rx_addr));
	wr_regl (dev, MASTER_RX_COUNT_REG_OFF, rx_bytes);
      }
      dev->rx_addr += rx_bytes;
    } else {
      if (irq)
	wr_regl (dev, MASTER_RX_COUNT_REG_OFF, 0);
      // allow another RX thread to start
      YELLOW_LED_ON(dev);
      clear_bit (rx_busy, &dev->flags);
      PRINTD (DBG_RX, "cleared rx_busy for dev %p", dev);
    }
    
#ifdef TAILRECURSIONWORKS
    // and we all bless optimised tail calls
    if (pio_instead)
      return rx_schedule (dev, 0);
    return;
#else
    // grrrrrrr!
    irq = 0;
  }
  return;
#endif
}

/********** handle RX bus master complete events **********/

static void rx_bus_master_complete_handler (hrz_dev * dev) {
  if (test_bit (rx_busy, &dev->flags)) {
    rx_schedule (dev, 1);
  } else {
    PRINTD (DBG_RX|DBG_ERR, "unexpected RX bus master completion");
    // clear interrupt condition on adapter
    wr_regl (dev, MASTER_RX_COUNT_REG_OFF, 0);
  }
  return;
}

/********** (queue to) become the next TX thread **********/

static int tx_hold (hrz_dev * dev) {
  PRINTD (DBG_TX, "sleeping at tx lock %p %lu", dev, dev->flags);
  wait_event_interruptible(dev->tx_queue, (!test_and_set_bit(tx_busy, &dev->flags)));
  PRINTD (DBG_TX, "woken at tx lock %p %lu", dev, dev->flags);
  if (signal_pending (current))
    return -1;
  PRINTD (DBG_TX, "set tx_busy for dev %p", dev);
  return 0;
}

/********** allow another TX thread to start **********/

static inline void tx_release (hrz_dev * dev) {
  clear_bit (tx_busy, &dev->flags);
  PRINTD (DBG_TX, "cleared tx_busy for dev %p", dev);
  wake_up_interruptible (&dev->tx_queue);
}

/********** schedule TX transfers **********/

static void tx_schedule (hrz_dev * const dev, int irq) {
  unsigned int tx_bytes;
  
  int append_desc = 0;
  
  int pio_instead = 0;
#ifndef TAILRECURSIONWORKS
  pio_instead = 1;
  while (pio_instead) {
#endif
    // bytes in current region waiting for TX transfer
    tx_bytes = dev->tx_bytes;
    
#if 0
    spin_count = 0;
    while (rd_regl (dev, MASTER_TX_COUNT_REG_OFF)) {
      PRINTD (DBG_TX|DBG_WARN, "TX error: other PCI Bus Master TX still in progress!");
      if (++spin_count > 10) {
	PRINTD (DBG_TX|DBG_ERR, "spun out waiting PCI Bus Master TX completion");
	wr_regl (dev, MASTER_TX_COUNT_REG_OFF, 0);
	tx_release (dev);
	hrz_kfree_skb (dev->tx_skb);
	return;
      }
    }
#endif
    
    if (tx_bytes) {
      // start next transfer within same region
      if (!test_bit (ultra, &dev->flags) || tx_bytes <= MAX_PIO_COUNT) {
	PRINTD (DBG_TX|DBG_BUS, "(pio)");
	pio_instead = 1;
      }
      if (tx_bytes <= MAX_TRANSFER_COUNT) {
	PRINTD (DBG_TX|DBG_BUS, "(simple or last multi)");
	if (!dev->tx_iovec) {
	  // end of last region
	  append_desc = 1;
	}
	dev->tx_bytes = 0;
      } else {
	PRINTD (DBG_TX|DBG_BUS, "(continuing multi)");
	dev->tx_bytes = tx_bytes - MAX_TRANSFER_COUNT;
	tx_bytes = MAX_TRANSFER_COUNT;
      }
    } else {
      // tx_bytes == 0 -- we're between regions
      // regions remaining to transfer
      unsigned int tx_regions = dev->tx_regions;
      
      if (tx_regions) {
	// start a new region
	dev->tx_addr = dev->tx_iovec->iov_base;
	tx_bytes = dev->tx_iovec->iov_len;
	++dev->tx_iovec;
	dev->tx_regions = tx_regions - 1;
	
	if (!test_bit (ultra, &dev->flags) || tx_bytes <= MAX_PIO_COUNT) {
	  PRINTD (DBG_TX|DBG_BUS, "(pio)");
	  pio_instead = 1;
	}
	if (tx_bytes <= MAX_TRANSFER_COUNT) {
	  PRINTD (DBG_TX|DBG_BUS, "(full region)");
	  dev->tx_bytes = 0;
	} else {
	  PRINTD (DBG_TX|DBG_BUS, "(start multi region)");
	  dev->tx_bytes = tx_bytes - MAX_TRANSFER_COUNT;
	  tx_bytes = MAX_TRANSFER_COUNT;
	}
      } else {
	// tx_regions == 0
	// that's all folks - end of frame
	struct sk_buff * skb = dev->tx_skb;
	dev->tx_iovec = NULL;
	
	// VC layer stats
	atomic_inc(&ATM_SKB(skb)->vcc->stats->tx);
	
	// free the skb
	hrz_kfree_skb (skb);
      }
    }
    
    // note: writing TX_COUNT clears any interrupt condition
    if (tx_bytes) {
      if (pio_instead) {
	if (irq)
	  wr_regl (dev, MASTER_TX_COUNT_REG_OFF, 0);
	wrs_regb (dev, DATA_PORT_OFF, dev->tx_addr, tx_bytes);
	if (append_desc)
	  wr_regl (dev, TX_DESCRIPTOR_PORT_OFF, cpu_to_be32 (dev->tx_skb->len));
      } else {
	wr_regl (dev, MASTER_TX_ADDR_REG_OFF, virt_to_bus (dev->tx_addr));
	if (append_desc)
	  wr_regl (dev, TX_DESCRIPTOR_REG_OFF, cpu_to_be32 (dev->tx_skb->len));
	wr_regl (dev, MASTER_TX_COUNT_REG_OFF,
		 append_desc
		 ? tx_bytes | MASTER_TX_AUTO_APPEND_DESC
		 : tx_bytes);
      }
      dev->tx_addr += tx_bytes;
    } else {
      if (irq)
	wr_regl (dev, MASTER_TX_COUNT_REG_OFF, 0);
      YELLOW_LED_ON(dev);
      tx_release (dev);
    }
    
#ifdef TAILRECURSIONWORKS
    // and we all bless optimised tail calls
    if (pio_instead)
      return tx_schedule (dev, 0);
    return;
#else
    // grrrrrrr!
    irq = 0;
  }
  return;
#endif
}

/********** handle TX bus master complete events **********/

static void tx_bus_master_complete_handler (hrz_dev * dev) {
  if (test_bit (tx_busy, &dev->flags)) {
    tx_schedule (dev, 1);
  } else {
    PRINTD (DBG_TX|DBG_ERR, "unexpected TX bus master completion");
    // clear interrupt condition on adapter
    wr_regl (dev, MASTER_TX_COUNT_REG_OFF, 0);
  }
  return;
}

/********** move RX Q pointer to next item in circular buffer **********/

// called only from IRQ sub-handler
static u32 rx_queue_entry_next (hrz_dev * dev) {
  u32 rx_queue_entry;
  spin_lock (&dev->mem_lock);
  rx_queue_entry = rd_mem (dev, &dev->rx_q_entry->entry);
  if (dev->rx_q_entry == dev->rx_q_wrap)
    dev->rx_q_entry = dev->rx_q_reset;
  else
    dev->rx_q_entry++;
  wr_regw (dev, RX_QUEUE_RD_PTR_OFF, dev->rx_q_entry - dev->rx_q_reset);
  spin_unlock (&dev->mem_lock);
  return rx_queue_entry;
}

/********** handle RX disabled by device **********/

static inline void rx_disabled_handler (hrz_dev * dev) {
  wr_regw (dev, RX_CONFIG_OFF, rd_regw (dev, RX_CONFIG_OFF) | RX_ENABLE);
  // count me please
  PRINTK (KERN_WARNING, "RX was disabled!");
}

/********** handle RX data received by device **********/

// called from IRQ handler
static void rx_data_av_handler (hrz_dev * dev) {
  u32 rx_queue_entry;
  u32 rx_queue_entry_flags;
  u16 rx_len;
  u16 rx_channel;
  
  PRINTD (DBG_FLOW, "hrz_data_av_handler");
  
  // try to grab rx lock (not possible during RX bus mastering)
  if (test_and_set_bit (rx_busy, &dev->flags)) {
    PRINTD (DBG_RX, "locked out of rx lock");
    return;
  }
  PRINTD (DBG_RX, "set rx_busy for dev %p", dev);
  // lock is cleared if we fail now, o/w after bus master completion
  
  YELLOW_LED_OFF(dev);
  
  rx_queue_entry = rx_queue_entry_next (dev);
  
  rx_len = rx_q_entry_to_length (rx_queue_entry);
  rx_channel = rx_q_entry_to_rx_channel (rx_queue_entry);
  
  WAIT_FLUSH_RX_COMPLETE (dev);
  
  SELECT_RX_CHANNEL (dev, rx_channel);
  
  PRINTD (DBG_RX, "rx_queue_entry is: %#x", rx_queue_entry);
  rx_queue_entry_flags = rx_queue_entry & (RX_CRC_32_OK|RX_COMPLETE_FRAME|SIMONS_DODGEY_MARKER);
  
  if (!rx_len) {
    // (at least) bus-mastering breaks if we try to handle a
    // zero-length frame, besides AAL5 does not support them
    PRINTK (KERN_ERR, "zero-length frame!");
    rx_queue_entry_flags &= ~RX_COMPLETE_FRAME;
  }
  
  if (rx_queue_entry_flags & SIMONS_DODGEY_MARKER) {
    PRINTD (DBG_RX|DBG_ERR, "Simon's marker detected!");
  }
  if (rx_queue_entry_flags == (RX_CRC_32_OK | RX_COMPLETE_FRAME)) {
    struct atm_vcc * atm_vcc;
    
    PRINTD (DBG_RX, "got a frame on rx_channel %x len %u", rx_channel, rx_len);
    
    atm_vcc = dev->rxer[rx_channel];
    // if no vcc is assigned to this channel, we should drop the frame
    // (is this what SIMONS etc. was trying to achieve?)
    
    if (atm_vcc) {
      
      if (atm_vcc->qos.rxtp.traffic_class != ATM_NONE) {
	
	if (rx_len <= atm_vcc->qos.rxtp.max_sdu) {
	    
	  struct sk_buff * skb = atm_alloc_charge (atm_vcc, rx_len, GFP_ATOMIC);
	  if (skb) {
	    // remember this so we can push it later
	    dev->rx_skb = skb;
	    // remember this so we can flush it later
	    dev->rx_channel = rx_channel;
	    
	    // prepare socket buffer
	    skb_put (skb, rx_len);
	    ATM_SKB(skb)->vcc = atm_vcc;
	    
	    // simple transfer
	    // dev->rx_regions = 0;
	    // dev->rx_iovec = 0;
	    dev->rx_bytes = rx_len;
	    dev->rx_addr = skb->data;
	    PRINTD (DBG_RX, "RX start simple transfer (addr %p, len %d)",
		    skb->data, rx_len);
	    
	    // do the business
	    rx_schedule (dev, 0);
	    return;
	    
	  } else {
	    PRINTD (DBG_SKB|DBG_WARN, "failed to get skb");
	  }
	  
	} else {
	  PRINTK (KERN_INFO, "frame received on TX-only VC %x", rx_channel);
	  // do we count this?
	}
	
      } else {
	PRINTK (KERN_WARNING, "dropped over-size frame");
	// do we count this?
      }
      
    } else {
      PRINTD (DBG_WARN|DBG_VCC|DBG_RX, "no VCC for this frame (VC closed)");
      // do we count this?
    }
    
  } else {
    // Wait update complete ? SPONG
  }
  
  // RX was aborted
  YELLOW_LED_ON(dev);
  
  FLUSH_RX_CHANNEL (dev,rx_channel);
  clear_bit (rx_busy, &dev->flags);
  
  return;
}

/********** interrupt handler **********/

static irqreturn_t interrupt_handler(int irq, void *dev_id)
{
  hrz_dev *dev = dev_id;
  u32 int_source;
  unsigned int irq_ok;
  
  PRINTD (DBG_FLOW, "interrupt_handler: %p", dev_id);
  
  // definitely for us
  irq_ok = 0;
  while ((int_source = rd_regl (dev, INT_SOURCE_REG_OFF)
	  & INTERESTING_INTERRUPTS)) {
    // In the interests of fairness, the handlers below are
    // called in sequence and without immediate return to the head of
    // the while loop. This is only of issue for slow hosts (or when
    // debugging messages are on). Really slow hosts may find a fast
    // sender keeps them permanently in the IRQ handler. :(
    
    // (only an issue for slow hosts) RX completion goes before
    // rx_data_av as the former implies rx_busy and so the latter
    // would just abort. If it reschedules another transfer
    // (continuing the same frame) then it will not clear rx_busy.
    
    // (only an issue for slow hosts) TX completion goes before RX
    // data available as it is a much shorter routine - there is the
    // chance that any further transfers it schedules will be complete
    // by the time of the return to the head of the while loop
    
    if (int_source & RX_BUS_MASTER_COMPLETE) {
      ++irq_ok;
      PRINTD (DBG_IRQ|DBG_BUS|DBG_RX, "rx_bus_master_complete asserted");
      rx_bus_master_complete_handler (dev);
    }
    if (int_source & TX_BUS_MASTER_COMPLETE) {
      ++irq_ok;
      PRINTD (DBG_IRQ|DBG_BUS|DBG_TX, "tx_bus_master_complete asserted");
      tx_bus_master_complete_handler (dev);
    }
    if (int_source & RX_DATA_AV) {
      ++irq_ok;
      PRINTD (DBG_IRQ|DBG_RX, "rx_data_av asserted");
      rx_data_av_handler (dev);
    }
  }
  if (irq_ok) {
    PRINTD (DBG_IRQ, "work done: %u", irq_ok);
  } else {
    PRINTD (DBG_IRQ|DBG_WARN, "spurious interrupt source: %#x", int_source);
  }
  
  PRINTD (DBG_IRQ|DBG_FLOW, "interrupt_handler done: %p", dev_id);
  if (irq_ok)
	return IRQ_HANDLED;
  return IRQ_NONE;
}

/********** housekeeping **********/

static void do_housekeeping (unsigned long arg) {
  // just stats at the moment
  hrz_dev * dev = (hrz_dev *) arg;

  // collect device-specific (not driver/atm-linux) stats here
  dev->tx_cell_count += rd_regw (dev, TX_CELL_COUNT_OFF);
  dev->rx_cell_count += rd_regw (dev, RX_CELL_COUNT_OFF);
  dev->hec_error_count += rd_regw (dev, HEC_ERROR_COUNT_OFF);
  dev->unassigned_cell_count += rd_regw (dev, UNASSIGNED_CELL_COUNT_OFF);

  mod_timer (&dev->housekeeping, jiffies + HZ/10);

  return;
}

/********** find an idle channel for TX and set it up **********/

// called with tx_busy set
static short setup_idle_tx_channel (hrz_dev * dev, hrz_vcc * vcc) {
  unsigned short idle_channels;
  short tx_channel = -1;
  unsigned int spin_count;
  PRINTD (DBG_FLOW|DBG_TX, "setup_idle_tx_channel %p", dev);
  
  // better would be to fail immediately, the caller can then decide whether
  // to wait or drop (depending on whether this is UBR etc.)
  spin_count = 0;
  while (!(idle_channels = rd_regw (dev, TX_STATUS_OFF) & IDLE_CHANNELS_MASK)) {
    PRINTD (DBG_TX|DBG_WARN, "waiting for idle TX channel");
    // delay a bit here
    if (++spin_count > 100) {
      PRINTD (DBG_TX|DBG_ERR, "spun out waiting for idle TX channel");
      return -EBUSY;
    }
  }
  
  // got an idle channel
  {
    // tx_idle ensures we look for idle channels in RR order
    int chan = dev->tx_idle;
    
    int keep_going = 1;
    while (keep_going) {
      if (idle_channels & (1<<chan)) {
	tx_channel = chan;
	keep_going = 0;
      }
      ++chan;
      if (chan == TX_CHANS)
	chan = 0;
    }
    
    dev->tx_idle = chan;
  }
  
  // set up the channel we found
  {
    // Initialise the cell header in the transmit channel descriptor
    // a.k.a. prepare the channel and remember that we have done so.
    
    tx_ch_desc * tx_desc = &memmap->tx_descs[tx_channel];
    u32 rd_ptr;
    u32 wr_ptr;
    u16 channel = vcc->channel;
    
    unsigned long flags;
    spin_lock_irqsave (&dev->mem_lock, flags);
    
    // Update the transmit channel record.
    dev->tx_channel_record[tx_channel] = channel;
    
    // xBR channel
    update_tx_channel_config (dev, tx_channel, RATE_TYPE_ACCESS,
			      vcc->tx_xbr_bits);
    
    // Update the PCR counter preload value etc.
    update_tx_channel_config (dev, tx_channel, PCR_TIMER_ACCESS,
			      vcc->tx_pcr_bits);

#if 0
    if (vcc->tx_xbr_bits == VBR_RATE_TYPE) {
      // SCR timer
      update_tx_channel_config (dev, tx_channel, SCR_TIMER_ACCESS,
				vcc->tx_scr_bits);
      
      // Bucket size...
      update_tx_channel_config (dev, tx_channel, BUCKET_CAPACITY_ACCESS,
				vcc->tx_bucket_bits);
      
      // ... and fullness
      update_tx_channel_config (dev, tx_channel, BUCKET_FULLNESS_ACCESS,
				vcc->tx_bucket_bits);
    }
#endif

    // Initialise the read and write buffer pointers
    rd_ptr = rd_mem (dev, &tx_desc->rd_buf_type) & BUFFER_PTR_MASK;
    wr_ptr = rd_mem (dev, &tx_desc->wr_buf_type) & BUFFER_PTR_MASK;
    
    // idle TX channels should have identical pointers
    if (rd_ptr != wr_ptr) {
      PRINTD (DBG_TX|DBG_ERR, "TX buffer pointers are broken!");
      // spin_unlock... return -E...
      // I wonder if gcc would get rid of one of the pointer aliases
    }
    PRINTD (DBG_TX, "TX buffer pointers are: rd %x, wr %x.",
	    rd_ptr, wr_ptr);
    
    switch (vcc->aal) {
      case aal0:
	PRINTD (DBG_QOS|DBG_TX, "tx_channel: aal0");
	rd_ptr |= CHANNEL_TYPE_RAW_CELLS;
	wr_ptr |= CHANNEL_TYPE_RAW_CELLS;
	break;
      case aal34:
	PRINTD (DBG_QOS|DBG_TX, "tx_channel: aal34");
	rd_ptr |= CHANNEL_TYPE_AAL3_4;
	wr_ptr |= CHANNEL_TYPE_AAL3_4;
	break;
      case aal5:
	rd_ptr |= CHANNEL_TYPE_AAL5;
	wr_ptr |= CHANNEL_TYPE_AAL5;
	// Initialise the CRC
	wr_mem (dev, &tx_desc->partial_crc, INITIAL_CRC);
	break;
    }
    
    wr_mem (dev, &tx_desc->rd_buf_type, rd_ptr);
    wr_mem (dev, &tx_desc->wr_buf_type, wr_ptr);
    
    // Write the Cell Header
    // Payload Type, CLP and GFC would go here if non-zero
    wr_mem (dev, &tx_desc->cell_header, channel);
    
    spin_unlock_irqrestore (&dev->mem_lock, flags);
  }
  
  return tx_channel;
}

/********** send a frame **********/

static int hrz_send (struct atm_vcc * atm_vcc, struct sk_buff * skb) {
  unsigned int spin_count;
  int free_buffers;
  hrz_dev * dev = HRZ_DEV(atm_vcc->dev);
  hrz_vcc * vcc = HRZ_VCC(atm_vcc);
  u16 channel = vcc->channel;
  
  u32 buffers_required;
  
  /* signed for error return */
  short tx_channel;
  
  PRINTD (DBG_FLOW|DBG_TX, "hrz_send vc %x data %p len %u",
	  channel, skb->data, skb->len);
  
  dump_skb (">>>", channel, skb);
  
  if (atm_vcc->qos.txtp.traffic_class == ATM_NONE) {
    PRINTK (KERN_ERR, "attempt to send on RX-only VC %x", channel);
    hrz_kfree_skb (skb);
    return -EIO;
  }
  
  // don't understand this
  ATM_SKB(skb)->vcc = atm_vcc;
  
  if (skb->len > atm_vcc->qos.txtp.max_sdu) {
    PRINTK (KERN_ERR, "sk_buff length greater than agreed max_sdu, dropping...");
    hrz_kfree_skb (skb);
    return -EIO;
  }
  
  if (!channel) {
    PRINTD (DBG_ERR|DBG_TX, "attempt to transmit on zero (rx_)channel");
    hrz_kfree_skb (skb);
    return -EIO;
  }
  
#if 0
  {
    // where would be a better place for this? housekeeping?
    u16 status;
    pci_read_config_word (dev->pci_dev, PCI_STATUS, &status);
    if (status & PCI_STATUS_REC_MASTER_ABORT) {
      PRINTD (DBG_BUS|DBG_ERR, "Clearing PCI Master Abort (and cleaning up)");
      status &= ~PCI_STATUS_REC_MASTER_ABORT;
      pci_write_config_word (dev->pci_dev, PCI_STATUS, status);
      if (test_bit (tx_busy, &dev->flags)) {
	hrz_kfree_skb (dev->tx_skb);
	tx_release (dev);
      }
    }
  }
#endif
  
#ifdef DEBUG_HORIZON
  /* wey-hey! */
  if (channel == 1023) {
    unsigned int i;
    unsigned short d = 0;
    char * s = skb->data;
    if (*s++ == 'D') {
	for (i = 0; i < 4; ++i)
		d = (d << 4) | hex_to_bin(*s++);
      PRINTK (KERN_INFO, "debug bitmap is now %hx", debug = d);
    }
  }
#endif
  
  // wait until TX is free and grab lock
  if (tx_hold (dev)) {
    hrz_kfree_skb (skb);
    return -ERESTARTSYS;
  }
 
  // Wait for enough space to be available in transmit buffer memory.
  
  // should be number of cells needed + 2 (according to hardware docs)
  // = ((framelen+8)+47) / 48 + 2
  // = (framelen+7) / 48 + 3, hmm... faster to put addition inside XXX
  buffers_required = (skb->len+(ATM_AAL5_TRAILER-1)) / ATM_CELL_PAYLOAD + 3;
  
  // replace with timer and sleep, add dev->tx_buffers_queue (max 1 entry)
  spin_count = 0;
  while ((free_buffers = rd_regw (dev, TX_FREE_BUFFER_COUNT_OFF)) < buffers_required) {
    PRINTD (DBG_TX, "waiting for free TX buffers, got %d of %d",
	    free_buffers, buffers_required);
    // what is the appropriate delay? implement a timeout? (depending on line speed?)
    // mdelay (1);
    // what happens if we kill (current_pid, SIGKILL) ?
    schedule();
    if (++spin_count > 1000) {
      PRINTD (DBG_TX|DBG_ERR, "spun out waiting for tx buffers, got %d of %d",
	      free_buffers, buffers_required);
      tx_release (dev);
      hrz_kfree_skb (skb);
      return -ERESTARTSYS;
    }
  }
  
  // Select a channel to transmit the frame on.
  if (channel == dev->last_vc) {
    PRINTD (DBG_TX, "last vc hack: hit");
    tx_channel = dev->tx_last;
  } else {
    PRINTD (DBG_TX, "last vc hack: miss");
    // Are we currently transmitting this VC on one of the channels?
    for (tx_channel = 0; tx_channel < TX_CHANS; ++tx_channel)
      if (dev->tx_channel_record[tx_channel] == channel) {
	PRINTD (DBG_TX, "vc already on channel: hit");
	break;
      }
    if (tx_channel == TX_CHANS) { 
      PRINTD (DBG_TX, "vc already on channel: miss");
      // Find and set up an idle channel.
      tx_channel = setup_idle_tx_channel (dev, vcc);
      if (tx_channel < 0) {
	PRINTD (DBG_TX|DBG_ERR, "failed to get channel");
	tx_release (dev);
	return tx_channel;
      }
    }
    
    PRINTD (DBG_TX, "got channel");
    SELECT_TX_CHANNEL(dev, tx_channel);
    
    dev->last_vc = channel;
    dev->tx_last = tx_channel;
  }
  
  PRINTD (DBG_TX, "using channel %u", tx_channel);
  
  YELLOW_LED_OFF(dev);
  
  // TX start transfer
  
  {
    unsigned int tx_len = skb->len;
    unsigned int tx_iovcnt = skb_shinfo(skb)->nr_frags;
    // remember this so we can free it later
    dev->tx_skb = skb;
    
    if (tx_iovcnt) {
      // scatter gather transfer
      dev->tx_regions = tx_iovcnt;
      dev->tx_iovec = NULL;		/* @@@ needs rewritten */
      dev->tx_bytes = 0;
      PRINTD (DBG_TX|DBG_BUS, "TX start scatter-gather transfer (iovec %p, len %d)",
	      skb->data, tx_len);
      tx_release (dev);
      hrz_kfree_skb (skb);
      return -EIO;
    } else {
      // simple transfer
      dev->tx_regions = 0;
      dev->tx_iovec = NULL;
      dev->tx_bytes = tx_len;
      dev->tx_addr = skb->data;
      PRINTD (DBG_TX|DBG_BUS, "TX start simple transfer (addr %p, len %d)",
	      skb->data, tx_len);
    }
    
    // and do the business
    tx_schedule (dev, 0);
    
  }
  
  return 0;
}

/********** reset a card **********/

static void hrz_reset (const hrz_dev * dev) {
  u32 control_0_reg = rd_regl (dev, CONTROL_0_REG);
  
  // why not set RESET_HORIZON to one and wait for the card to
  // reassert that bit as zero? Like so:
  control_0_reg = control_0_reg & RESET_HORIZON;
  wr_regl (dev, CONTROL_0_REG, control_0_reg);
  while (control_0_reg & RESET_HORIZON)
    control_0_reg = rd_regl (dev, CONTROL_0_REG);
  
  // old reset code retained:
  wr_regl (dev, CONTROL_0_REG, control_0_reg |
	   RESET_ATM | RESET_RX | RESET_TX | RESET_HOST);
  // just guessing here
  udelay (1000);
  
  wr_regl (dev, CONTROL_0_REG, control_0_reg);
}

/********** read the burnt in address **********/

static void WRITE_IT_WAIT (const hrz_dev *dev, u32 ctrl)
{
	wr_regl (dev, CONTROL_0_REG, ctrl);
	udelay (5);
}
  
static void CLOCK_IT (const hrz_dev *dev, u32 ctrl)
{
	// DI must be valid around rising SK edge
	WRITE_IT_WAIT(dev, ctrl & ~SEEPROM_SK);
	WRITE_IT_WAIT(dev, ctrl | SEEPROM_SK);
}

static u16 __devinit read_bia (const hrz_dev * dev, u16 addr)
{
  u32 ctrl = rd_regl (dev, CONTROL_0_REG);
  
  const unsigned int addr_bits = 6;
  const unsigned int data_bits = 16;
  
  unsigned int i;
  
  u16 res;
  
  ctrl &= ~(SEEPROM_CS | SEEPROM_SK | SEEPROM_DI);
  WRITE_IT_WAIT(dev, ctrl);
  
  // wake Serial EEPROM and send 110 (READ) command
  ctrl |=  (SEEPROM_CS | SEEPROM_DI);
  CLOCK_IT(dev, ctrl);
  
  ctrl |= SEEPROM_DI;
  CLOCK_IT(dev, ctrl);
  
  ctrl &= ~SEEPROM_DI;
  CLOCK_IT(dev, ctrl);
  
  for (i=0; i<addr_bits; i++) {
    if (addr & (1 << (addr_bits-1)))
      ctrl |= SEEPROM_DI;
    else
      ctrl &= ~SEEPROM_DI;
    
    CLOCK_IT(dev, ctrl);
    
    addr = addr << 1;
  }
  
  // we could check that we have DO = 0 here
  ctrl &= ~SEEPROM_DI;
  
  res = 0;
  for (i=0;i<data_bits;i++) {
    res = res >> 1;
    
    CLOCK_IT(dev, ctrl);
    
    if (rd_regl (dev, CONTROL_0_REG) & SEEPROM_DO)
      res |= (1 << (data_bits-1));
  }
  
  ctrl &= ~(SEEPROM_SK | SEEPROM_CS);
  WRITE_IT_WAIT(dev, ctrl);
  
  return res;
}

/********** initialise a card **********/

static int __devinit hrz_init (hrz_dev * dev) {
  int onefivefive;
  
  u16 chan;
  
  int buff_count;
  
  HDW * mem;
  
  cell_buf * tx_desc;
  cell_buf * rx_desc;
  
  u32 ctrl;
  
  ctrl = rd_regl (dev, CONTROL_0_REG);
  PRINTD (DBG_INFO, "ctrl0reg is %#x", ctrl);
  onefivefive = ctrl & ATM_LAYER_STATUS;
  
  if (onefivefive)
    printk (DEV_LABEL ": Horizon Ultra (at 155.52 MBps)");
  else
    printk (DEV_LABEL ": Horizon (at 25 MBps)");
  
  printk (":");
  // Reset the card to get everything in a known state
  
  printk (" reset");
  hrz_reset (dev);
  
  // Clear all the buffer memory
  
  printk (" clearing memory");
  
  for (mem = (HDW *) memmap; mem < (HDW *) (memmap + 1); ++mem)
    wr_mem (dev, mem, 0);
  
  printk (" tx channels");
  
  // All transmit eight channels are set up as AAL5 ABR channels with
  // a 16us cell spacing. Why?
  
  // Channel 0 gets the free buffer at 100h, channel 1 gets the free
  // buffer at 110h etc.
  
  for (chan = 0; chan < TX_CHANS; ++chan) {
    tx_ch_desc * tx_desc = &memmap->tx_descs[chan];
    cell_buf * buf = &memmap->inittxbufs[chan];
    
    // initialise the read and write buffer pointers
    wr_mem (dev, &tx_desc->rd_buf_type, BUF_PTR(buf));
    wr_mem (dev, &tx_desc->wr_buf_type, BUF_PTR(buf));
    
    // set the status of the initial buffers to empty
    wr_mem (dev, &buf->next, BUFF_STATUS_EMPTY);
  }
  
  // Use space bufn3 at the moment for tx buffers
  
  printk (" tx buffers");
  
  tx_desc = memmap->bufn3;
  
  wr_mem (dev, &memmap->txfreebufstart.next, BUF_PTR(tx_desc) | BUFF_STATUS_EMPTY);
  
  for (buff_count = 0; buff_count < BUFN3_SIZE-1; buff_count++) {
    wr_mem (dev, &tx_desc->next, BUF_PTR(tx_desc+1) | BUFF_STATUS_EMPTY);
    tx_desc++;
  }
  
  wr_mem (dev, &tx_desc->next, BUF_PTR(&memmap->txfreebufend) | BUFF_STATUS_EMPTY);
  
  // Initialise the transmit free buffer count
  wr_regw (dev, TX_FREE_BUFFER_COUNT_OFF, BUFN3_SIZE);
  
  printk (" rx channels");
  
  // Initialise all of the receive channels to be AAL5 disabled with
  // an interrupt threshold of 0
  
  for (chan = 0; chan < RX_CHANS; ++chan) {
    rx_ch_desc * rx_desc = &memmap->rx_descs[chan];
    
    wr_mem (dev, &rx_desc->wr_buf_type, CHANNEL_TYPE_AAL5 | RX_CHANNEL_DISABLED);
  }
  
  printk (" rx buffers");
  
  // Use space bufn4 at the moment for rx buffers
  
  rx_desc = memmap->bufn4;
  
  wr_mem (dev, &memmap->rxfreebufstart.next, BUF_PTR(rx_desc) | BUFF_STATUS_EMPTY);
  
  for (buff_count = 0; buff_count < BUFN4_SIZE-1; buff_count++) {
    wr_mem (dev, &rx_desc->next, BUF_PTR(rx_desc+1) | BUFF_STATUS_EMPTY);
    
    rx_desc++;
  }
  
  wr_mem (dev, &rx_desc->next, BUF_PTR(&memmap->rxfreebufend) | BUFF_STATUS_EMPTY);
  
  // Initialise the receive free buffer count
  wr_regw (dev, RX_FREE_BUFFER_COUNT_OFF, BUFN4_SIZE);
  
  // Initialize Horizons registers
  
  // TX config
  wr_regw (dev, TX_CONFIG_OFF,
	   ABR_ROUND_ROBIN | TX_NORMAL_OPERATION | DRVR_DRVRBAR_ENABLE);
  
  // RX config. Use 10-x VC bits, x VP bits, non user cells in channel 0.
  wr_regw (dev, RX_CONFIG_OFF,
	   DISCARD_UNUSED_VPI_VCI_BITS_SET | NON_USER_CELLS_IN_ONE_CHANNEL | vpi_bits);
  
  // RX line config
  wr_regw (dev, RX_LINE_CONFIG_OFF,
	   LOCK_DETECT_ENABLE | FREQUENCY_DETECT_ENABLE | GXTALOUT_SELECT_DIV4);
  
  // Set the max AAL5 cell count to be just enough to contain the
  // largest AAL5 frame that the user wants to receive
  wr_regw (dev, MAX_AAL5_CELL_COUNT_OFF,
	   DIV_ROUND_UP(max_rx_size + ATM_AAL5_TRAILER, ATM_CELL_PAYLOAD));
  
  // Enable receive
  wr_regw (dev, RX_CONFIG_OFF, rd_regw (dev, RX_CONFIG_OFF) | RX_ENABLE);
  
  printk (" control");
  
  // Drive the OE of the LEDs then turn the green LED on
  ctrl |= GREEN_LED_OE | YELLOW_LED_OE | GREEN_LED | YELLOW_LED;
  wr_regl (dev, CONTROL_0_REG, ctrl);
  
  // Test for a 155-capable card
  
  if (onefivefive) {
    // Select 155 mode... make this a choice (or: how do we detect
    // external line speed and switch?)
    ctrl |= ATM_LAYER_SELECT;
    wr_regl (dev, CONTROL_0_REG, ctrl);
    
    // test SUNI-lite vs SAMBA
    
    // Register 0x00 in the SUNI will have some of bits 3-7 set, and
    // they will always be zero for the SAMBA.  Ha!  Bloody hardware
    // engineers.  It'll never work.
    
    if (rd_framer (dev, 0) & 0x00f0) {
      // SUNI
      printk (" SUNI");
      
      // Reset, just in case
      wr_framer (dev, 0x00, 0x0080);
      wr_framer (dev, 0x00, 0x0000);
      
      // Configure transmit FIFO
      wr_framer (dev, 0x63, rd_framer (dev, 0x63) | 0x0002);
      
      // Set line timed mode
      wr_framer (dev, 0x05, rd_framer (dev, 0x05) | 0x0001);
    } else {
      // SAMBA
      printk (" SAMBA");
      
      // Reset, just in case
      wr_framer (dev, 0, rd_framer (dev, 0) | 0x0001);
      wr_framer (dev, 0, rd_framer (dev, 0) &~ 0x0001);
      
      // Turn off diagnostic loopback and enable line-timed mode
      wr_framer (dev, 0, 0x0002);
      
      // Turn on transmit outputs
      wr_framer (dev, 2, 0x0B80);
    }
  } else {
    // Select 25 mode
    ctrl &= ~ATM_LAYER_SELECT;
    
    // Madge B154 setup
    // none required?
  }
  
  printk (" LEDs");
  
  GREEN_LED_ON(dev);
  YELLOW_LED_ON(dev);
  
  printk (" ESI=");
  
  {
    u16 b = 0;
    int i;
    u8 * esi = dev->atm_dev->esi;
    
    // in the card I have, EEPROM
    // addresses 0, 1, 2 contain 0
    // addresess 5, 6 etc. contain ffff
    // NB: Madge prefix is 00 00 f6 (which is 00 00 6f in Ethernet bit order)
    // the read_bia routine gets the BIA in Ethernet bit order
    
    for (i=0; i < ESI_LEN; ++i) {
      if (i % 2 == 0)
	b = read_bia (dev, i/2 + 2);
      else
	b = b >> 8;
      esi[i] = b & 0xFF;
      printk ("%02x", esi[i]);
    }
  }
  
  // Enable RX_Q and ?X_COMPLETE interrupts only
  wr_regl (dev, INT_ENABLE_REG_OFF, INTERESTING_INTERRUPTS);
  printk (" IRQ on");
  
  printk (".\n");
  
  return onefivefive;
}

/********** check max_sdu **********/

static int check_max_sdu (hrz_aal aal, struct atm_trafprm * tp, unsigned int max_frame_size) {
  PRINTD (DBG_FLOW|DBG_QOS, "check_max_sdu");
  
  switch (aal) {
    case aal0:
      if (!(tp->max_sdu)) {
	PRINTD (DBG_QOS, "defaulting max_sdu");
	tp->max_sdu = ATM_AAL0_SDU;
      } else if (tp->max_sdu != ATM_AAL0_SDU) {
	PRINTD (DBG_QOS|DBG_ERR, "rejecting max_sdu");
	return -EINVAL;
      }
      break;
    case aal34:
      if (tp->max_sdu == 0 || tp->max_sdu > ATM_MAX_AAL34_PDU) {
	PRINTD (DBG_QOS, "%sing max_sdu", tp->max_sdu ? "capp" : "default");
	tp->max_sdu = ATM_MAX_AAL34_PDU;
      }
      break;
    case aal5:
      if (tp->max_sdu == 0 || tp->max_sdu > max_frame_size) {
	PRINTD (DBG_QOS, "%sing max_sdu", tp->max_sdu ? "capp" : "default");
	tp->max_sdu = max_frame_size;
      }
      break;
  }
  return 0;
}

/********** check pcr **********/

// something like this should be part of ATM Linux
static int atm_pcr_check (struct atm_trafprm * tp, unsigned int pcr) {
  // we are assuming non-UBR, and non-special values of pcr
  if (tp->min_pcr == ATM_MAX_PCR)
    PRINTD (DBG_QOS, "luser gave min_pcr = ATM_MAX_PCR");
  else if (tp->min_pcr < 0)
    PRINTD (DBG_QOS, "luser gave negative min_pcr");
  else if (tp->min_pcr && tp->min_pcr > pcr)
    PRINTD (DBG_QOS, "pcr less than min_pcr");
  else
    // !! max_pcr = UNSPEC (0) is equivalent to max_pcr = MAX (-1)
    // easier to #define ATM_MAX_PCR 0 and have all rates unsigned?
    // [this would get rid of next two conditionals]
    if ((0) && tp->max_pcr == ATM_MAX_PCR)
      PRINTD (DBG_QOS, "luser gave max_pcr = ATM_MAX_PCR");
    else if ((tp->max_pcr != ATM_MAX_PCR) && tp->max_pcr < 0)
      PRINTD (DBG_QOS, "luser gave negative max_pcr");
    else if (tp->max_pcr && tp->max_pcr != ATM_MAX_PCR && tp->max_pcr < pcr)
      PRINTD (DBG_QOS, "pcr greater than max_pcr");
    else {
      // each limit unspecified or not violated
      PRINTD (DBG_QOS, "xBR(pcr) OK");
      return 0;
    }
  PRINTD (DBG_QOS, "pcr=%u, tp: min_pcr=%d, pcr=%d, max_pcr=%d",
	  pcr, tp->min_pcr, tp->pcr, tp->max_pcr);
  return -EINVAL;
}

/********** open VC **********/

static int hrz_open (struct atm_vcc *atm_vcc)
{
  int error;
  u16 channel;
  
  struct atm_qos * qos;
  struct atm_trafprm * txtp;
  struct atm_trafprm * rxtp;
  
  hrz_dev * dev = HRZ_DEV(atm_vcc->dev);
  hrz_vcc vcc;
  hrz_vcc * vccp; // allocated late
  short vpi = atm_vcc->vpi;
  int vci = atm_vcc->vci;
  PRINTD (DBG_FLOW|DBG_VCC, "hrz_open %x %x", vpi, vci);
  
#ifdef ATM_VPI_UNSPEC
  // UNSPEC is deprecated, remove this code eventually
  if (vpi == ATM_VPI_UNSPEC || vci == ATM_VCI_UNSPEC) {
    PRINTK (KERN_WARNING, "rejecting open with unspecified VPI/VCI (deprecated)");
    return -EINVAL;
  }
#endif
  
  error = vpivci_to_channel (&channel, vpi, vci);
  if (error) {
    PRINTD (DBG_WARN|DBG_VCC, "VPI/VCI out of range: %hd/%d", vpi, vci);
    return error;
  }
  
  vcc.channel = channel;
  // max speed for the moment
  vcc.tx_rate = 0x0;
  
  qos = &atm_vcc->qos;
  
  // check AAL and remember it
  switch (qos->aal) {
    case ATM_AAL0:
      // we would if it were 48 bytes and not 52!
      PRINTD (DBG_QOS|DBG_VCC, "AAL0");
      vcc.aal = aal0;
      break;
    case ATM_AAL34:
      // we would if I knew how do the SAR!
      PRINTD (DBG_QOS|DBG_VCC, "AAL3/4");
      vcc.aal = aal34;
      break;
    case ATM_AAL5:
      PRINTD (DBG_QOS|DBG_VCC, "AAL5");
      vcc.aal = aal5;
      break;
    default:
      PRINTD (DBG_QOS|DBG_VCC, "Bad AAL!");
      return -EINVAL;
  }
  
  // TX traffic parameters
  
  // there are two, interrelated problems here: 1. the reservation of
  // PCR is not a binary choice, we are given bounds and/or a
  // desirable value; 2. the device is only capable of certain values,
  // most of which are not integers. It is almost certainly acceptable
  // to be off by a maximum of 1 to 10 cps.
  
  // Pragmatic choice: always store an integral PCR as that which has
  // been allocated, even if we allocate a little (or a lot) less,
  // after rounding. The actual allocation depends on what we can
  // manage with our rate selection algorithm. The rate selection
  // algorithm is given an integral PCR and a tolerance and told
  // whether it should round the value up or down if the tolerance is
  // exceeded; it returns: a) the actual rate selected (rounded up to
  // the nearest integer), b) a bit pattern to feed to the timer
  // register, and c) a failure value if no applicable rate exists.
  
  // Part of the job is done by atm_pcr_goal which gives us a PCR
  // specification which says: EITHER grab the maximum available PCR
  // (and perhaps a lower bound which we musn't pass), OR grab this
  // amount, rounding down if you have to (and perhaps a lower bound
  // which we musn't pass) OR grab this amount, rounding up if you
  // have to (and perhaps an upper bound which we musn't pass). If any
  // bounds ARE passed we fail. Note that rounding is only rounding to
  // match device limitations, we do not round down to satisfy
  // bandwidth availability even if this would not violate any given
  // lower bound.
  
  // Note: telephony = 64kb/s = 48 byte cell payload @ 500/3 cells/s
  // (say) so this is not even a binary fixpoint cell rate (but this
  // device can do it). To avoid this sort of hassle we use a
  // tolerance parameter (currently fixed at 10 cps).
  
  PRINTD (DBG_QOS, "TX:");
  
  txtp = &qos->txtp;
  
  // set up defaults for no traffic
  vcc.tx_rate = 0;
  // who knows what would actually happen if you try and send on this?
  vcc.tx_xbr_bits = IDLE_RATE_TYPE;
  vcc.tx_pcr_bits = CLOCK_DISABLE;
#if 0
  vcc.tx_scr_bits = CLOCK_DISABLE;
  vcc.tx_bucket_bits = 0;
#endif
  
  if (txtp->traffic_class != ATM_NONE) {
    error = check_max_sdu (vcc.aal, txtp, max_tx_size);
    if (error) {
      PRINTD (DBG_QOS, "TX max_sdu check failed");
      return error;
    }
    
    switch (txtp->traffic_class) {
      case ATM_UBR: {
	// we take "the PCR" as a rate-cap
	// not reserved
	vcc.tx_rate = 0;
	make_rate (dev, 1<<30, round_nearest, &vcc.tx_pcr_bits, NULL);
	vcc.tx_xbr_bits = ABR_RATE_TYPE;
	break;
      }
#if 0
      case ATM_ABR: {
	// reserve min, allow up to max
	vcc.tx_rate = 0; // ?
	make_rate (dev, 1<<30, round_nearest, &vcc.tx_pcr_bits, 0);
	vcc.tx_xbr_bits = ABR_RATE_TYPE;
	break;
      }
#endif
      case ATM_CBR: {
	int pcr = atm_pcr_goal (txtp);
	rounding r;
	if (!pcr) {
	  // down vs. up, remaining bandwidth vs. unlimited bandwidth!!
	  // should really have: once someone gets unlimited bandwidth
	  // that no more non-UBR channels can be opened until the
	  // unlimited one closes?? For the moment, round_down means
	  // greedy people actually get something and not nothing
	  r = round_down;
	  // slight race (no locking) here so we may get -EAGAIN
	  // later; the greedy bastards would deserve it :)
	  PRINTD (DBG_QOS, "snatching all remaining TX bandwidth");
	  pcr = dev->tx_avail;
	} else if (pcr < 0) {
	  r = round_down;
	  pcr = -pcr;
	} else {
	  r = round_up;
	}
	error = make_rate_with_tolerance (dev, pcr, r, 10,
					  &vcc.tx_pcr_bits, &vcc.tx_rate);
	if (error) {
	  PRINTD (DBG_QOS, "could not make rate from TX PCR");
	  return error;
	}
	// not really clear what further checking is needed
	error = atm_pcr_check (txtp, vcc.tx_rate);
	if (error) {
	  PRINTD (DBG_QOS, "TX PCR failed consistency check");
	  return error;
	}
	vcc.tx_xbr_bits = CBR_RATE_TYPE;
	break;
      }
#if 0
      case ATM_VBR: {
	int pcr = atm_pcr_goal (txtp);
	// int scr = atm_scr_goal (txtp);
	int scr = pcr/2; // just for fun
	unsigned int mbs = 60; // just for fun
	rounding pr;
	rounding sr;
	unsigned int bucket;
	if (!pcr) {
	  pr = round_nearest;
	  pcr = 1<<30;
	} else if (pcr < 0) {
	  pr = round_down;
	  pcr = -pcr;
	} else {
	  pr = round_up;
	}
	error = make_rate_with_tolerance (dev, pcr, pr, 10,
					  &vcc.tx_pcr_bits, 0);
	if (!scr) {
	  // see comments for PCR with CBR above
	  sr = round_down;
	  // slight race (no locking) here so we may get -EAGAIN
	  // later; the greedy bastards would deserve it :)
	  PRINTD (DBG_QOS, "snatching all remaining TX bandwidth");
	  scr = dev->tx_avail;
	} else if (scr < 0) {
	  sr = round_down;
	  scr = -scr;
	} else {
	  sr = round_up;
	}
	error = make_rate_with_tolerance (dev, scr, sr, 10,
					  &vcc.tx_scr_bits, &vcc.tx_rate);
	if (error) {
	  PRINTD (DBG_QOS, "could not make rate from TX SCR");
	  return error;
	}
	// not really clear what further checking is needed
	// error = atm_scr_check (txtp, vcc.tx_rate);
	if (error) {
	  PRINTD (DBG_QOS, "TX SCR failed consistency check");
	  return error;
	}
	// bucket calculations (from a piece of paper...) cell bucket
	// capacity must be largest integer smaller than m(p-s)/p + 1
	// where m = max burst size, p = pcr, s = scr
	bucket = mbs*(pcr-scr)/pcr;
	if (bucket*pcr != mbs*(pcr-scr))
	  bucket += 1;
	if (bucket > BUCKET_MAX_SIZE) {
	  PRINTD (DBG_QOS, "shrinking bucket from %u to %u",
		  bucket, BUCKET_MAX_SIZE);
	  bucket = BUCKET_MAX_SIZE;
	}
	vcc.tx_xbr_bits = VBR_RATE_TYPE;
	vcc.tx_bucket_bits = bucket;
	break;
      }
#endif
      default: {
	PRINTD (DBG_QOS, "unsupported TX traffic class");
	return -EINVAL;
      }
    }
  }
  
  // RX traffic parameters
  
  PRINTD (DBG_QOS, "RX:");
  
  rxtp = &qos->rxtp;
  
  // set up defaults for no traffic
  vcc.rx_rate = 0;
  
  if (rxtp->traffic_class != ATM_NONE) {
    error = check_max_sdu (vcc.aal, rxtp, max_rx_size);
    if (error) {
      PRINTD (DBG_QOS, "RX max_sdu check failed");
      return error;
    }
    switch (rxtp->traffic_class) {
      case ATM_UBR: {
	// not reserved
	break;
      }
#if 0
      case ATM_ABR: {
	// reserve min
	vcc.rx_rate = 0; // ?
	break;
      }
#endif
      case ATM_CBR: {
	int pcr = atm_pcr_goal (rxtp);
	if (!pcr) {
	  // slight race (no locking) here so we may get -EAGAIN
	  // later; the greedy bastards would deserve it :)
	  PRINTD (DBG_QOS, "snatching all remaining RX bandwidth");
	  pcr = dev->rx_avail;
	} else if (pcr < 0) {
	  pcr = -pcr;
	}
	vcc.rx_rate = pcr;
	// not really clear what further checking is needed
	error = atm_pcr_check (rxtp, vcc.rx_rate);
	if (error) {
	  PRINTD (DBG_QOS, "RX PCR failed consistency check");
	  return error;
	}
	break;
      }
#if 0
      case ATM_VBR: {
	// int scr = atm_scr_goal (rxtp);
	int scr = 1<<16; // just for fun
	if (!scr) {
	  // slight race (no locking) here so we may get -EAGAIN
	  // later; the greedy bastards would deserve it :)
	  PRINTD (DBG_QOS, "snatching all remaining RX bandwidth");
	  scr = dev->rx_avail;
	} else if (scr < 0) {
	  scr = -scr;
	}
	vcc.rx_rate = scr;
	// not really clear what further checking is needed
	// error = atm_scr_check (rxtp, vcc.rx_rate);
	if (error) {
	  PRINTD (DBG_QOS, "RX SCR failed consistency check");
	  return error;
	}
	break;
      }
#endif
      default: {
	PRINTD (DBG_QOS, "unsupported RX traffic class");
	return -EINVAL;
      }
    }
  }
  
  
  // late abort useful for diagnostics
  if (vcc.aal != aal5) {
    PRINTD (DBG_QOS, "AAL not supported");
    return -EINVAL;
  }
  
  // get space for our vcc stuff and copy parameters into it
  vccp = kmalloc (sizeof(hrz_vcc), GFP_KERNEL);
  if (!vccp) {
    PRINTK (KERN_ERR, "out of memory!");
    return -ENOMEM;
  }
  *vccp = vcc;
  
  // clear error and grab cell rate resource lock
  error = 0;
  spin_lock (&dev->rate_lock);
  
  if (vcc.tx_rate > dev->tx_avail) {
    PRINTD (DBG_QOS, "not enough TX PCR left");
    error = -EAGAIN;
  }
  
  if (vcc.rx_rate > dev->rx_avail) {
    PRINTD (DBG_QOS, "not enough RX PCR left");
    error = -EAGAIN;
  }
  
  if (!error) {
    // really consume cell rates
    dev->tx_avail -= vcc.tx_rate;
    dev->rx_avail -= vcc.rx_rate;
    PRINTD (DBG_QOS|DBG_VCC, "reserving %u TX PCR and %u RX PCR",
	    vcc.tx_rate, vcc.rx_rate);
  }
  
  // release lock and exit on error
  spin_unlock (&dev->rate_lock);
  if (error) {
    PRINTD (DBG_QOS|DBG_VCC, "insufficient cell rate resources");
    kfree (vccp);
    return error;
  }
  
  // this is "immediately before allocating the connection identifier
  // in hardware" - so long as the next call does not fail :)
  set_bit(ATM_VF_ADDR,&atm_vcc->flags);
  
  // any errors here are very serious and should never occur
  
  if (rxtp->traffic_class != ATM_NONE) {
    if (dev->rxer[channel]) {
      PRINTD (DBG_ERR|DBG_VCC, "VC already open for RX");
      error = -EBUSY;
    }
    if (!error)
      error = hrz_open_rx (dev, channel);
    if (error) {
      kfree (vccp);
      return error;
    }
    // this link allows RX frames through
    dev->rxer[channel] = atm_vcc;
  }
  
  // success, set elements of atm_vcc
  atm_vcc->dev_data = (void *) vccp;
  
  // indicate readiness
  set_bit(ATM_VF_READY,&atm_vcc->flags);
  
  return 0;
}

/********** close VC **********/

static void hrz_close (struct atm_vcc * atm_vcc) {
  hrz_dev * dev = HRZ_DEV(atm_vcc->dev);
  hrz_vcc * vcc = HRZ_VCC(atm_vcc);
  u16 channel = vcc->channel;
  PRINTD (DBG_VCC|DBG_FLOW, "hrz_close");
  
  // indicate unreadiness
  clear_bit(ATM_VF_READY,&atm_vcc->flags);

  if (atm_vcc->qos.txtp.traffic_class != ATM_NONE) {
    unsigned int i;
    
    // let any TX on this channel that has started complete
    // no restart, just keep trying
    while (tx_hold (dev))
      ;
    // remove record of any tx_channel having been setup for this channel
    for (i = 0; i < TX_CHANS; ++i)
      if (dev->tx_channel_record[i] == channel) {
	dev->tx_channel_record[i] = -1;
	break;
      }
    if (dev->last_vc == channel)
      dev->tx_last = -1;
    tx_release (dev);
  }

  if (atm_vcc->qos.rxtp.traffic_class != ATM_NONE) {
    // disable RXing - it tries quite hard
    hrz_close_rx (dev, channel);
    // forget the vcc - no more skbs will be pushed
    if (atm_vcc != dev->rxer[channel])
      PRINTK (KERN_ERR, "%s atm_vcc=%p rxer[channel]=%p",
	      "arghhh! we're going to die!",
	      atm_vcc, dev->rxer[channel]);
    dev->rxer[channel] = NULL;
  }
  
  // atomically release our rate reservation
  spin_lock (&dev->rate_lock);
  PRINTD (DBG_QOS|DBG_VCC, "releasing %u TX PCR and %u RX PCR",
	  vcc->tx_rate, vcc->rx_rate);
  dev->tx_avail += vcc->tx_rate;
  dev->rx_avail += vcc->rx_rate;
  spin_unlock (&dev->rate_lock);
  
  // free our structure
  kfree (vcc);
  // say the VPI/VCI is free again
  clear_bit(ATM_VF_ADDR,&atm_vcc->flags);
}

#if 0
static int hrz_getsockopt (struct atm_vcc * atm_vcc, int level, int optname,
			   void *optval, int optlen) {
  hrz_dev * dev = HRZ_DEV(atm_vcc->dev);
  PRINTD (DBG_FLOW|DBG_VCC, "hrz_getsockopt");
  switch (level) {
    case SOL_SOCKET:
      switch (optname) {
//	case SO_BCTXOPT:
//	  break;
//	case SO_BCRXOPT:
//	  break;
	default:
	  return -ENOPROTOOPT;
      };
      break;
  }
  return -EINVAL;
}

static int hrz_setsockopt (struct atm_vcc * atm_vcc, int level, int optname,
			   void *optval, unsigned int optlen) {
  hrz_dev * dev = HRZ_DEV(atm_vcc->dev);
  PRINTD (DBG_FLOW|DBG_VCC, "hrz_setsockopt");
  switch (level) {
    case SOL_SOCKET:
      switch (optname) {
//	case SO_BCTXOPT:
//	  break;
//	case SO_BCRXOPT:
//	  break;
	default:
	  return -ENOPROTOOPT;
      };
      break;
  }
  return -EINVAL;
}
#endif

#if 0
static int hrz_ioctl (struct atm_dev * atm_dev, unsigned int cmd, void *arg) {
  hrz_dev * dev = HRZ_DEV(atm_dev);
  PRINTD (DBG_FLOW, "hrz_ioctl");
  return -1;
}

unsigned char hrz_phy_get (struct atm_dev * atm_dev, unsigned long addr) {
  hrz_dev * dev = HRZ_DEV(atm_dev);
  PRINTD (DBG_FLOW, "hrz_phy_get");
  return 0;
}

static void hrz_phy_put (struct atm_dev * atm_dev, unsigned char value,
			 unsigned long addr) {
  hrz_dev * dev = HRZ_DEV(atm_dev);
  PRINTD (DBG_FLOW, "hrz_phy_put");
}

static int hrz_change_qos (struct atm_vcc * atm_vcc, struct atm_qos *qos, int flgs) {
  hrz_dev * dev = HRZ_DEV(vcc->dev);
  PRINTD (DBG_FLOW, "hrz_change_qos");
  return -1;
}
#endif

/********** proc file contents **********/

static int hrz_proc_read (struct atm_dev * atm_dev, loff_t * pos, char * page) {
  hrz_dev * dev = HRZ_DEV(atm_dev);
  int left = *pos;
  PRINTD (DBG_FLOW, "hrz_proc_read");
  
  /* more diagnostics here? */
  
#if 0
  if (!left--) {
    unsigned int count = sprintf (page, "vbr buckets:");
    unsigned int i;
    for (i = 0; i < TX_CHANS; ++i)
      count += sprintf (page, " %u/%u",
			query_tx_channel_config (dev, i, BUCKET_FULLNESS_ACCESS),
			query_tx_channel_config (dev, i, BUCKET_CAPACITY_ACCESS));
    count += sprintf (page+count, ".\n");
    return count;
  }
#endif
  
  if (!left--)
    return sprintf (page,
		    "cells: TX %lu, RX %lu, HEC errors %lu, unassigned %lu.\n",
		    dev->tx_cell_count, dev->rx_cell_count,
		    dev->hec_error_count, dev->unassigned_cell_count);
  
  if (!left--)
    return sprintf (page,
		    "free cell buffers: TX %hu, RX %hu+%hu.\n",
		    rd_regw (dev, TX_FREE_BUFFER_COUNT_OFF),
		    rd_regw (dev, RX_FREE_BUFFER_COUNT_OFF),
		    dev->noof_spare_buffers);
  
  if (!left--)
    return sprintf (page,
		    "cps remaining: TX %u, RX %u\n",
		    dev->tx_avail, dev->rx_avail);
  
  return 0;
}

static const struct atmdev_ops hrz_ops = {
  .open	= hrz_open,
  .close	= hrz_close,
  .send	= hrz_send,
  .proc_read	= hrz_proc_read,
  .owner	= THIS_MODULE,
};

static int __devinit hrz_probe(struct pci_dev *pci_dev, const struct pci_device_id *pci_ent)
{
	hrz_dev * dev;
	int err = 0;

	// adapter slot free, read resources from PCI configuration space
	u32 iobase = pci_resource_start (pci_dev, 0);
	u32 * membase = bus_to_virt (pci_resource_start (pci_dev, 1));
	unsigned int irq;
	unsigned char lat;

	PRINTD (DBG_FLOW, "hrz_probe");

	if (pci_enable_device(pci_dev))
		return -EINVAL;

	/* XXX DEV_LABEL is a guess */
	if (!request_region(iobase, HRZ_IO_EXTENT, DEV_LABEL)) {
		err = -EINVAL;
		goto out_disable;
	}

	dev = kzalloc(sizeof(hrz_dev), GFP_KERNEL);
	if (!dev) {
		// perhaps we should be nice: deregister all adapters and abort?
		PRINTD(DBG_ERR, "out of memory");
		err = -ENOMEM;
		goto out_release;
	}

	pci_set_drvdata(pci_dev, dev);

	// grab IRQ and install handler - move this someplace more sensible
	irq = pci_dev->irq;
	if (request_irq(irq,
			interrupt_handler,
			IRQF_SHARED, /* irqflags guess */
			DEV_LABEL, /* name guess */
			dev)) {
		PRINTD(DBG_WARN, "request IRQ failed!");
		err = -EINVAL;
		goto out_free;
	}

	PRINTD(DBG_INFO, "found Madge ATM adapter (hrz) at: IO %x, IRQ %u, MEM %p",
	       iobase, irq, membase);

	dev->atm_dev = atm_dev_register(DEV_LABEL, &pci_dev->dev, &hrz_ops, -1,
					NULL);
	if (!(dev->atm_dev)) {
		PRINTD(DBG_ERR, "failed to register Madge ATM adapter");
		err = -EINVAL;
		goto out_free_irq;
	}

	PRINTD(DBG_INFO, "registered Madge ATM adapter (no. %d) (%p) at %p",
	       dev->atm_dev->number, dev, dev->atm_dev);
	dev->atm_dev->dev_data = (void *) dev;
	dev->pci_dev = pci_dev; 

	// enable bus master accesses
	pci_set_master(pci_dev);

	// frobnicate latency (upwards, usually)
	pci_read_config_byte(pci_dev, PCI_LATENCY_TIMER, &lat);
	if (pci_lat) {
		PRINTD(DBG_INFO, "%s PCI latency timer from %hu to %hu",
		       "changing", lat, pci_lat);
		pci_write_config_byte(pci_dev, PCI_LATENCY_TIMER, pci_lat);
	} else if (lat < MIN_PCI_LATENCY) {
		PRINTK(KERN_INFO, "%s PCI latency timer from %hu to %hu",
		       "increasing", lat, MIN_PCI_LATENCY);
		pci_write_config_byte(pci_dev, PCI_LATENCY_TIMER, MIN_PCI_LATENCY);
	}

	dev->iobase = iobase;
	dev->irq = irq; 
	dev->membase = membase; 

	dev->rx_q_entry = dev->rx_q_reset = &memmap->rx_q_entries[0];
	dev->rx_q_wrap  = &memmap->rx_q_entries[RX_CHANS-1];

	// these next three are performance hacks
	dev->last_vc = -1;
	dev->tx_last = -1;
	dev->tx_idle = 0;

	dev->tx_regions = 0;
	dev->tx_bytes = 0;
	dev->tx_skb = NULL;
	dev->tx_iovec = NULL;

	dev->tx_cell_count = 0;
	dev->rx_cell_count = 0;
	dev->hec_error_count = 0;
	dev->unassigned_cell_count = 0;

	dev->noof_spare_buffers = 0;

	{
		unsigned int i;
		for (i = 0; i < TX_CHANS; ++i)
			dev->tx_channel_record[i] = -1;
	}

	dev->flags = 0;

	// Allocate cell rates and remember ASIC version
	// Fibre: ATM_OC3_PCR = 1555200000/8/270*260/53 - 29/53
	// Copper: (WRONG) we want 6 into the above, close to 25Mb/s
	// Copper: (plagarise!) 25600000/8/270*260/53 - n/53

	if (hrz_init(dev)) {
		// to be really pedantic, this should be ATM_OC3c_PCR
		dev->tx_avail = ATM_OC3_PCR;
		dev->rx_avail = ATM_OC3_PCR;
		set_bit(ultra, &dev->flags); // NOT "|= ultra" !
	} else {
		dev->tx_avail = ((25600000/8)*26)/(27*53);
		dev->rx_avail = ((25600000/8)*26)/(27*53);
		PRINTD(DBG_WARN, "Buggy ASIC: no TX bus-mastering.");
	}

	// rate changes spinlock
	spin_lock_init(&dev->rate_lock);

	// on-board memory access spinlock; we want atomic reads and
	// writes to adapter memory (handles IRQ and SMP)
	spin_lock_init(&dev->mem_lock);

	init_waitqueue_head(&dev->tx_queue);

	// vpi in 0..4, vci in 6..10
	dev->atm_dev->ci_range.vpi_bits = vpi_bits;
	dev->atm_dev->ci_range.vci_bits = 10-vpi_bits;

	init_timer(&dev->housekeeping);
	dev->housekeeping.function = do_housekeeping;
	dev->housekeeping.data = (unsigned long) dev;
	mod_timer(&dev->housekeeping, jiffies);

out:
	return err;

out_free_irq:
	free_irq(dev->irq, dev);
out_free:
	kfree(dev);
out_release:
	release_region(iobase, HRZ_IO_EXTENT);
out_disable:
	pci_disable_device(pci_dev);
	goto out;
}

static void __devexit hrz_remove_one(struct pci_dev *pci_dev)
{
	hrz_dev *dev;

	dev = pci_get_drvdata(pci_dev);

	PRINTD(DBG_INFO, "closing %p (atm_dev = %p)", dev, dev->atm_dev);
	del_timer_sync(&dev->housekeeping);
	hrz_reset(dev);
	atm_dev_deregister(dev->atm_dev);
	free_irq(dev->irq, dev);
	release_region(dev->iobase, HRZ_IO_EXTENT);
	kfree(dev);

	pci_disable_device(pci_dev);
}

static void __init hrz_check_args (void) {
#ifdef DEBUG_HORIZON
  PRINTK (KERN_NOTICE, "debug bitmap is %hx", debug &= DBG_MASK);
#else
  if (debug)
    PRINTK (KERN_NOTICE, "no debug support in this image");
#endif
  
  if (vpi_bits > HRZ_MAX_VPI)
    PRINTK (KERN_ERR, "vpi_bits has been limited to %hu",
	    vpi_bits = HRZ_MAX_VPI);
  
  if (max_tx_size < 0 || max_tx_size > TX_AAL5_LIMIT)
    PRINTK (KERN_NOTICE, "max_tx_size has been limited to %hu",
	    max_tx_size = TX_AAL5_LIMIT);
  
  if (max_rx_size < 0 || max_rx_size > RX_AAL5_LIMIT)
    PRINTK (KERN_NOTICE, "max_rx_size has been limited to %hu",
	    max_rx_size = RX_AAL5_LIMIT);
  
  return;
}

MODULE_AUTHOR(maintainer_string);
MODULE_DESCRIPTION(description_string);
MODULE_LICENSE("GPL");
module_param(debug, ushort, 0644);
module_param(vpi_bits, ushort, 0);
module_param(max_tx_size, int, 0);
module_param(max_rx_size, int, 0);
module_param(pci_lat, byte, 0);
MODULE_PARM_DESC(debug, "debug bitmap, see .h file");
MODULE_PARM_DESC(vpi_bits, "number of bits (0..4) to allocate to VPIs");
MODULE_PARM_DESC(max_tx_size, "maximum size of TX AAL5 frames");
MODULE_PARM_DESC(max_rx_size, "maximum size of RX AAL5 frames");
MODULE_PARM_DESC(pci_lat, "PCI latency in bus cycles");

static struct pci_device_id hrz_pci_tbl[] = {
	{ PCI_VENDOR_ID_MADGE, PCI_DEVICE_ID_MADGE_HORIZON, PCI_ANY_ID, PCI_ANY_ID,
	  0, 0, 0 },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, hrz_pci_tbl);

static struct pci_driver hrz_driver = {
	.name =		"horizon",
	.probe =	hrz_probe,
	.remove =	__devexit_p(hrz_remove_one),
	.id_table =	hrz_pci_tbl,
};

/********** module entry **********/

static int __init hrz_module_init (void) {
  // sanity check - cast is needed since printk does not support %Zu
  if (sizeof(struct MEMMAP) != 128*1024/4) {
    PRINTK (KERN_ERR, "Fix struct MEMMAP (is %lu fakewords).",
	    (unsigned long) sizeof(struct MEMMAP));
    return -ENOMEM;
  }
  
  show_version();
  
  // check arguments
  hrz_check_args();
  
  // get the juice
  return pci_register_driver(&hrz_driver);
}

/********** module exit **********/

static void __exit hrz_module_exit (void) {
  PRINTD (DBG_FLOW, "cleanup_module");

  pci_unregister_driver(&hrz_driver);
}

module_init(hrz_module_init);
module_exit(hrz_module_exit);
