/*
 * Copyright 2011 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

/**
 * Interface definitions for the mpipe driver.
 */

#ifndef _SYS_HV_DRV_MPIPE_INTF_H
#define _SYS_HV_DRV_MPIPE_INTF_H

#include <arch/mpipe.h>
#include <arch/mpipe_constants.h>


/** Number of buffer stacks (32). */
#define HV_MPIPE_NUM_BUFFER_STACKS \
  (MPIPE_MMIO_INIT_DAT_GX36_1__BUFFER_STACK_MASK_WIDTH)

/** Number of NotifRings (256). */
#define HV_MPIPE_NUM_NOTIF_RINGS (MPIPE_NUM_NOTIF_RINGS)

/** Number of NotifGroups (32). */
#define HV_MPIPE_NUM_NOTIF_GROUPS (MPIPE_NUM_NOTIF_GROUPS)

/** Number of buckets (4160). */
#define HV_MPIPE_NUM_BUCKETS (MPIPE_NUM_BUCKETS)

/** Number of "lo" buckets (4096). */
#define HV_MPIPE_NUM_LO_BUCKETS 4096

/** Number of "hi" buckets (64). */
#define HV_MPIPE_NUM_HI_BUCKETS \
  (HV_MPIPE_NUM_BUCKETS - HV_MPIPE_NUM_LO_BUCKETS)

/** Number of edma rings (24). */
#define HV_MPIPE_NUM_EDMA_RINGS \
  (MPIPE_MMIO_INIT_DAT_GX36_1__EDMA_POST_MASK_WIDTH)




/** A flag bit indicating a fixed resource allocation. */
#define HV_MPIPE_ALLOC_FIXED 0x01

/** Offset for the config register MMIO region. */
#define HV_MPIPE_CONFIG_MMIO_OFFSET \
  (MPIPE_MMIO_ADDR__REGION_VAL_CFG << MPIPE_MMIO_ADDR__REGION_SHIFT)

/** Size of the config register MMIO region. */
#define HV_MPIPE_CONFIG_MMIO_SIZE (64 * 1024)

/** Offset for the config register MMIO region. */
#define HV_MPIPE_FAST_MMIO_OFFSET \
  (MPIPE_MMIO_ADDR__REGION_VAL_IDMA << MPIPE_MMIO_ADDR__REGION_SHIFT)

/** Size of the fast register MMIO region (IDMA, EDMA, buffer stack). */
#define HV_MPIPE_FAST_MMIO_SIZE \
  ((MPIPE_MMIO_ADDR__REGION_VAL_BSM + 1 - MPIPE_MMIO_ADDR__REGION_VAL_IDMA) \
   << MPIPE_MMIO_ADDR__REGION_SHIFT)


/*
 * Each type of resource allocation comes in quantized chunks, where
 * XXX_BITS is the number of chunks, and XXX_RES_PER_BIT is the number
 * of resources in each chunk.
 */

/** Number of buffer stack chunks available (32). */
#define HV_MPIPE_ALLOC_BUFFER_STACKS_BITS \
  MPIPE_MMIO_INIT_DAT_GX36_1__BUFFER_STACK_MASK_WIDTH

/** Granularity of buffer stack allocation (1). */
#define HV_MPIPE_ALLOC_BUFFER_STACKS_RES_PER_BIT \
  (HV_MPIPE_NUM_BUFFER_STACKS / HV_MPIPE_ALLOC_BUFFER_STACKS_BITS)

/** Number of NotifRing chunks available (32). */
#define HV_MPIPE_ALLOC_NOTIF_RINGS_BITS \
  MPIPE_MMIO_INIT_DAT_GX36_0__NOTIF_RING_MASK_WIDTH

/** Granularity of NotifRing allocation (8). */
#define HV_MPIPE_ALLOC_NOTIF_RINGS_RES_PER_BIT \
  (HV_MPIPE_NUM_NOTIF_RINGS / HV_MPIPE_ALLOC_NOTIF_RINGS_BITS)

/** Number of NotifGroup chunks available (32). */
#define HV_MPIPE_ALLOC_NOTIF_GROUPS_BITS \
  HV_MPIPE_NUM_NOTIF_GROUPS

/** Granularity of NotifGroup allocation (1). */
#define HV_MPIPE_ALLOC_NOTIF_GROUPS_RES_PER_BIT \
  (HV_MPIPE_NUM_NOTIF_GROUPS / HV_MPIPE_ALLOC_NOTIF_GROUPS_BITS)

/** Number of lo bucket chunks available (16). */
#define HV_MPIPE_ALLOC_LO_BUCKETS_BITS \
  MPIPE_MMIO_INIT_DAT_GX36_0__BUCKET_RELEASE_MASK_LO_WIDTH

/** Granularity of lo bucket allocation (256). */
#define HV_MPIPE_ALLOC_LO_BUCKETS_RES_PER_BIT \
  (HV_MPIPE_NUM_LO_BUCKETS / HV_MPIPE_ALLOC_LO_BUCKETS_BITS)

/** Number of hi bucket chunks available (16). */
#define HV_MPIPE_ALLOC_HI_BUCKETS_BITS \
  MPIPE_MMIO_INIT_DAT_GX36_0__BUCKET_RELEASE_MASK_HI_WIDTH

/** Granularity of hi bucket allocation (4). */
#define HV_MPIPE_ALLOC_HI_BUCKETS_RES_PER_BIT \
  (HV_MPIPE_NUM_HI_BUCKETS / HV_MPIPE_ALLOC_HI_BUCKETS_BITS)

/** Number of eDMA ring chunks available (24). */
#define HV_MPIPE_ALLOC_EDMA_RINGS_BITS \
  MPIPE_MMIO_INIT_DAT_GX36_1__EDMA_POST_MASK_WIDTH

/** Granularity of eDMA ring allocation (1). */
#define HV_MPIPE_ALLOC_EDMA_RINGS_RES_PER_BIT \
  (HV_MPIPE_NUM_EDMA_RINGS / HV_MPIPE_ALLOC_EDMA_RINGS_BITS)




/** Bit vector encoding which NotifRings are in a NotifGroup. */
typedef struct
{
  /** The actual bits. */
  uint64_t ring_mask[4];

} gxio_mpipe_notif_group_bits_t;


/** Another name for MPIPE_LBL_INIT_DAT_BSTS_TBL_t. */
typedef MPIPE_LBL_INIT_DAT_BSTS_TBL_t gxio_mpipe_bucket_info_t;



/** Eight buffer stack ids. */
typedef struct
{
  /** The stacks. */
  uint8_t stacks[8];

} gxio_mpipe_rules_stacks_t;


/** A destination mac address. */
typedef struct
{
  /** The octets. */
  uint8_t octets[6];

} gxio_mpipe_rules_dmac_t;


/** A vlan. */
typedef uint16_t gxio_mpipe_rules_vlan_t;



/** Maximum number of characters in a link name. */
#define GXIO_MPIPE_LINK_NAME_LEN  32


/** Structure holding a link name.  Only needed, and only typedef'ed,
 *  because the IORPC stub generator only handles types which are single
 *  words coming before the parameter name. */
typedef struct
{
  /** The name itself. */
  char name[GXIO_MPIPE_LINK_NAME_LEN];
}
_gxio_mpipe_link_name_t;

/** Maximum number of characters in a symbol name. */
#define GXIO_MPIPE_SYMBOL_NAME_LEN  128


/** Structure holding a symbol name.  Only needed, and only typedef'ed,
 *  because the IORPC stub generator only handles types which are single
 *  words coming before the parameter name. */
typedef struct
{
  /** The name itself. */
  char name[GXIO_MPIPE_SYMBOL_NAME_LEN];
}
_gxio_mpipe_symbol_name_t;


/** Structure holding a MAC address. */
typedef struct
{
  /** The address. */
  uint8_t mac[6];
}
_gxio_mpipe_link_mac_t;



/** Request shared data permission -- that is, the ability to send and
 *  receive packets -- on the specified link.  Other processes may also
 *  request shared data permission on the same link.
 *
 *  No more than one of ::GXIO_MPIPE_LINK_DATA, ::GXIO_MPIPE_LINK_NO_DATA,
 *  or ::GXIO_MPIPE_LINK_EXCL_DATA may be specifed in a gxio_mpipe_link_open()
 *  call.  If none are specified, ::GXIO_MPIPE_LINK_DATA is assumed.
 */
#define GXIO_MPIPE_LINK_DATA               0x00000001UL

/** Do not request data permission on the specified link.
 *
 *  No more than one of ::GXIO_MPIPE_LINK_DATA, ::GXIO_MPIPE_LINK_NO_DATA,
 *  or ::GXIO_MPIPE_LINK_EXCL_DATA may be specifed in a gxio_mpipe_link_open()
 *  call.  If none are specified, ::GXIO_MPIPE_LINK_DATA is assumed.
 */
#define GXIO_MPIPE_LINK_NO_DATA            0x00000002UL

/** Request exclusive data permission -- that is, the ability to send and
 *  receive packets -- on the specified link.  No other processes may
 *  request data permission on this link, and if any process already has
 *  data permission on it, this open will fail.
 *
 *  No more than one of ::GXIO_MPIPE_LINK_DATA, ::GXIO_MPIPE_LINK_NO_DATA,
 *  or ::GXIO_MPIPE_LINK_EXCL_DATA may be specifed in a gxio_mpipe_link_open()
 *  call.  If none are specified, ::GXIO_MPIPE_LINK_DATA is assumed.
 */
#define GXIO_MPIPE_LINK_EXCL_DATA          0x00000004UL

/** Request shared stats permission -- that is, the ability to read and write
 *  registers which contain link statistics, and to get link attributes --
 *  on the specified link.  Other processes may also request shared stats
 *  permission on the same link.
 *
 *  No more than one of ::GXIO_MPIPE_LINK_STATS, ::GXIO_MPIPE_LINK_NO_STATS,
 *  or ::GXIO_MPIPE_LINK_EXCL_STATS may be specifed in a gxio_mpipe_link_open()
 *  call.  If none are specified, ::GXIO_MPIPE_LINK_STATS is assumed.
 */
#define GXIO_MPIPE_LINK_STATS              0x00000008UL

/** Do not request stats permission on the specified link.
 *
 *  No more than one of ::GXIO_MPIPE_LINK_STATS, ::GXIO_MPIPE_LINK_NO_STATS,
 *  or ::GXIO_MPIPE_LINK_EXCL_STATS may be specifed in a gxio_mpipe_link_open()
 *  call.  If none are specified, ::GXIO_MPIPE_LINK_STATS is assumed.
 */
#define GXIO_MPIPE_LINK_NO_STATS           0x00000010UL

/** Request exclusive stats permission -- that is, the ability to read and
 *  write registers which contain link statistics, and to get link
 *  attributes -- on the specified link.  No other processes may request
 *  stats permission on this link, and if any process already
 *  has stats permission on it, this open will fail.
 *
 *  Requesting exclusive stats permission is normally a very bad idea, since
 *  it prevents programs like mpipe-stat from providing information on this
 *  link.  Applications should only do this if they use MAC statistics
 *  registers, and cannot tolerate any of the clear-on-read registers being
 *  reset by other statistics programs.
 *
 *  No more than one of ::GXIO_MPIPE_LINK_STATS, ::GXIO_MPIPE_LINK_NO_STATS,
 *  or ::GXIO_MPIPE_LINK_EXCL_STATS may be specifed in a gxio_mpipe_link_open()
 *  call.  If none are specified, ::GXIO_MPIPE_LINK_STATS is assumed.
 */
#define GXIO_MPIPE_LINK_EXCL_STATS         0x00000020UL

/** Request shared control permission -- that is, the ability to modify link
 *  attributes, and read and write MAC and MDIO registers -- on the
 *  specified link.  Other processes may also request shared control
 *  permission on the same link.
 *
 *  No more than one of ::GXIO_MPIPE_LINK_CTL, ::GXIO_MPIPE_LINK_NO_CTL,
 *  or ::GXIO_MPIPE_LINK_EXCL_CTL may be specifed in a gxio_mpipe_link_open()
 *  call.  If none are specified, ::GXIO_MPIPE_LINK_CTL is assumed.
 */
#define GXIO_MPIPE_LINK_CTL                0x00000040UL

/** Do not request control permission on the specified link.
 *
 *  No more than one of ::GXIO_MPIPE_LINK_CTL, ::GXIO_MPIPE_LINK_NO_CTL,
 *  or ::GXIO_MPIPE_LINK_EXCL_CTL may be specifed in a gxio_mpipe_link_open()
 *  call.  If none are specified, ::GXIO_MPIPE_LINK_CTL is assumed.
 */
#define GXIO_MPIPE_LINK_NO_CTL             0x00000080UL

/** Request exclusive control permission -- that is, the ability to modify
 *  link attributes, and read and write MAC and MDIO registers -- on the
 *  specified link.  No other processes may request control permission on
 *  this link, and if any process already has control permission on it,
 *  this open will fail.
 *
 *  Requesting exclusive control permission is not always a good idea, since
 *  it prevents programs like mpipe-link from configuring the link.
 *
 *  No more than one of ::GXIO_MPIPE_LINK_CTL, ::GXIO_MPIPE_LINK_NO_CTL,
 *  or ::GXIO_MPIPE_LINK_EXCL_CTL may be specifed in a gxio_mpipe_link_open()
 *  call.  If none are specified, ::GXIO_MPIPE_LINK_CTL is assumed.
 */
#define GXIO_MPIPE_LINK_EXCL_CTL           0x00000100UL

/** Set the desired state of the link to up, allowing any speeds which are
 *  supported by the link hardware, as part of this open operation; do not
 *  change the desired state of the link when it is closed or the process
 *  exits.  No more than one of ::GXIO_MPIPE_LINK_AUTO_UP,
 *  ::GXIO_MPIPE_LINK_AUTO_UPDOWN, ::GXIO_MPIPE_LINK_AUTO_DOWN, or
 *  ::GXIO_MPIPE_LINK_AUTO_NONE may be specifed in a gxio_mpipe_link_open()
 *  call.  If none are specified, ::GXIO_MPIPE_LINK_AUTO_UPDOWN is assumed.
 */
#define GXIO_MPIPE_LINK_AUTO_UP            0x00000200UL

/** Set the desired state of the link to up, allowing any speeds which are
 *  supported by the link hardware, as part of this open operation; when the
 *  link is closed or this process exits, if no other process has the link
 *  open, set the desired state of the link to down.  No more than one of
 *  ::GXIO_MPIPE_LINK_AUTO_UP, ::GXIO_MPIPE_LINK_AUTO_UPDOWN,
 *  ::GXIO_MPIPE_LINK_AUTO_DOWN, or ::GXIO_MPIPE_LINK_AUTO_NONE may be
 *  specifed in a gxio_mpipe_link_open() call.  If none are specified,
 *  ::GXIO_MPIPE_LINK_AUTO_UPDOWN is assumed.
 */
#define GXIO_MPIPE_LINK_AUTO_UPDOWN        0x00000400UL

/** Do not change the desired state of the link as part of the open
 *  operation; when the link is closed or this process exits, if no other
 *  process has the link open, set the desired state of the link to down.
 *  No more than one of ::GXIO_MPIPE_LINK_AUTO_UP,
 *  ::GXIO_MPIPE_LINK_AUTO_UPDOWN, ::GXIO_MPIPE_LINK_AUTO_DOWN, or
 *  ::GXIO_MPIPE_LINK_AUTO_NONE may be specifed in a gxio_mpipe_link_open()
 *  call.  If none are specified, ::GXIO_MPIPE_LINK_AUTO_UPDOWN is assumed.
 */
#define GXIO_MPIPE_LINK_AUTO_DOWN          0x00000800UL

/** Do not change the desired state of the link as part of the open
 *  operation; do not change the desired state of the link when it is
 *  closed or the process exits.  No more than one of
 *  ::GXIO_MPIPE_LINK_AUTO_UP, ::GXIO_MPIPE_LINK_AUTO_UPDOWN,
 *  ::GXIO_MPIPE_LINK_AUTO_DOWN, or ::GXIO_MPIPE_LINK_AUTO_NONE may be
 *  specifed in a gxio_mpipe_link_open() call.  If none are specified,
 *  ::GXIO_MPIPE_LINK_AUTO_UPDOWN is assumed.
 */
#define GXIO_MPIPE_LINK_AUTO_NONE          0x00001000UL

/** Request that this open call not complete until the network link is up.
 *  The process will wait as long as necessary for this to happen;
 *  applications which wish to abandon waiting for the link after a
 *  specific time period should not specify this flag when opening a link,
 *  but should instead call gxio_mpipe_link_wait() afterward.  The link
 *  must be opened with stats permission.  Note that this flag by itself
 *  does not change the desired link state; if other open flags or previous
 *  link state changes have not requested a desired state of up, the open
 *  call will never complete.  This flag is not available to kernel
 *  clients.
 */
#define GXIO_MPIPE_LINK_WAIT               0x00002000UL


/*
 * Note: link attributes must fit in 24 bits, since we use the top 8 bits
 * of the IORPC offset word for the channel number.
 */

/** Determine whether jumbo frames may be received.  If this attribute's
 *  value value is nonzero, the MAC will accept frames of up to 10240 bytes.
 *  If the value is zero, the MAC will only accept frames of up to 1544
 *  bytes.  The default value is zero. */
#define GXIO_MPIPE_LINK_RECEIVE_JUMBO      0x010000

/** Determine whether to send pause frames on this link if the mPIPE packet
 *  FIFO is nearly full.  If the value is zero, pause frames are not sent.
 *  If the value is nonzero, it is the delay value which will be sent in any
 *  pause frames which are output, in units of 512 bit times.
 *
 *  Bear in mind that in almost all circumstances, the mPIPE packet FIFO
 *  will never fill up, since mPIPE will empty it as fast as or faster than
 *  the incoming data rate, by either delivering or dropping packets.  The
 *  only situation in which this is not true is if the memory and cache
 *  subsystem is extremely heavily loaded, and mPIPE cannot perform DMA of
 *  packet data to memory in a timely fashion.  In particular, pause frames
 *  will <em>not</em> be sent if packets cannot be delivered because
 *  NotifRings are full, buckets are full, or buffers are not available in
 *  a buffer stack. */
#define GXIO_MPIPE_LINK_SEND_PAUSE         0x020000

/** Determine whether to suspend output on the receipt of pause frames.
 *  If the value is nonzero, mPIPE shim will suspend output on the link's
 *  channel when a pause frame is received.  If the value is zero, pause
 *  frames will be ignored.  The default value is zero. */
#define GXIO_MPIPE_LINK_RECEIVE_PAUSE      0x030000

/** Interface MAC address.  The value is a 6-byte MAC address, in the least
 *  significant 48 bits of the value; in other words, an address which would
 *  be printed as '12:34:56:78:90:AB' in IEEE 802 canonical format would
 *  be returned as 0x12345678ab.
 *
 *  Depending upon the overall system design, a MAC address may or may not
 *  be available for each interface.  Note that the interface's MAC address
 *  does not limit the packets received on its channel, although the
 *  classifier's rules could be configured to do that.  Similarly, the MAC
 *  address is not used when transmitting packets, although applications
 *  could certainly decide to use the assigned address as a source MAC
 *  address when doing so.  This attribute may only be retrieved with
 *  gxio_mpipe_link_get_attr(); it may not be modified.
 */
#define GXIO_MPIPE_LINK_MAC                0x040000

/** Determine whether to discard egress packets on link down. If this value
 *  is nonzero, packets sent on this link while the link is down will be
 *  discarded.  If this value is zero, no packets will be sent on this link
 *  while it is down.  The default value is one. */
#define GXIO_MPIPE_LINK_DISCARD_IF_DOWN    0x050000

/** Possible link state.  The value is a combination of link state flags,
 *  ORed together, that indicate link modes which are actually supported by
 *  the hardware.  This attribute may only be retrieved with
 *  gxio_mpipe_link_get_attr(); it may not be modified. */
#define GXIO_MPIPE_LINK_POSSIBLE_STATE     0x060000

/** Current link state.  The value is a combination of link state flags,
 *  ORed together, that indicate the current state of the hardware.  If the
 *  link is down, the value ANDed with ::GXIO_MPIPE_LINK_SPEED will be zero;
 *  if the link is up, the value ANDed with ::GXIO_MPIPE_LINK_SPEED will
 *  result in exactly one of the speed values, indicating the current speed.
 *  This attribute may only be retrieved with gxio_mpipe_link_get_attr(); it
 *  may not be modified. */
#define GXIO_MPIPE_LINK_CURRENT_STATE      0x070000

/** Desired link state. The value is a conbination of flags, which specify
 *  the desired state for the link.  With gxio_mpipe_link_set_attr(), this
 *  will, in the background, attempt to bring up the link using whichever of
 *  the requested flags are reasonable, or take down the link if the flags
 *  are zero.  The actual link up or down operation may happen after this
 *  call completes.  If the link state changes in the future, the system
 *  will continue to try to get back to the desired link state; for
 *  instance, if the link is brought up successfully, and then the network
 *  cable is disconnected, the link will go down.  However, the desired
 *  state of the link is still up, so if the cable is reconnected, the link
 *  will be brought up again.
 *
 *  With gxio_mpipe_link_set_attr(), this will indicate the desired state
 *  for the link, as set with a previous gxio_mpipe_link_set_attr() call,
 *  or implicitly by a gxio_mpipe_link_open() or link close operation.
 *  This may not reflect the current state of the link; to get that, use
 *  ::GXIO_MPIPE_LINK_CURRENT_STATE.
 */
#define GXIO_MPIPE_LINK_DESIRED_STATE      0x080000



/** Link can run, should run, or is running at 10 Mbps. */
#define GXIO_MPIPE_LINK_10M        0x0000000000000001UL

/** Link can run, should run, or is running at 100 Mbps. */
#define GXIO_MPIPE_LINK_100M       0x0000000000000002UL

/** Link can run, should run, or is running at 1 Gbps. */
#define GXIO_MPIPE_LINK_1G         0x0000000000000004UL

/** Link can run, should run, or is running at 10 Gbps. */
#define GXIO_MPIPE_LINK_10G        0x0000000000000008UL

/** Link can run, should run, or is running at 20 Gbps. */
#define GXIO_MPIPE_LINK_20G        0x0000000000000010UL

/** Link can run, should run, or is running at 25 Gbps. */
#define GXIO_MPIPE_LINK_25G        0x0000000000000020UL

/** Link can run, should run, or is running at 50 Gbps. */
#define GXIO_MPIPE_LINK_50G        0x0000000000000040UL

/** Link should run at the highest speed supported by the link and by
 *  the device connected to the link.  Only usable as a value for
 *  the link's desired state; never returned as a value for the current
 *  or possible states. */
#define GXIO_MPIPE_LINK_ANYSPEED   0x0000000000000800UL

/** All legal link speeds.  This value is provided for use in extracting
 *  the speed-related subset of the link state flags; it is not intended
 *  to be set directly as a value for one of the GXIO_MPIPE_LINK_xxx_STATE
 *  attributes.  A link is up or is requested to be up if its current or
 *  desired state, respectively, ANDED with this value, is nonzero. */
#define GXIO_MPIPE_LINK_SPEED_MASK 0x0000000000000FFFUL

/** Link can run, should run, or is running in MAC loopback mode.  This
 *  loops transmitted packets back to the receiver, inside the Tile
 *  Processor. */
#define GXIO_MPIPE_LINK_LOOP_MAC   0x0000000000001000UL

/** Link can run, should run, or is running in PHY loopback mode.  This
 *  loops transmitted packets back to the receiver, inside the external
 *  PHY chip. */
#define GXIO_MPIPE_LINK_LOOP_PHY   0x0000000000002000UL

/** Link can run, should run, or is running in external loopback mode.
 *  This requires that an external loopback plug be installed on the
 *  Ethernet port.  Note that only some links require that this be
 *  configured via the gxio_mpipe_link routines; other links can do
 *  external loopack with the plug and no special configuration. */
#define GXIO_MPIPE_LINK_LOOP_EXT   0x0000000000004000UL

/** All legal loopback types. */
#define GXIO_MPIPE_LINK_LOOP_MASK  0x000000000000F000UL

/** Link can run, should run, or is running in full-duplex mode.
 *  If neither ::GXIO_MPIPE_LINK_FDX nor ::GXIO_MPIPE_LINK_HDX are
 *  specified in a set of desired state flags, both are assumed. */
#define GXIO_MPIPE_LINK_FDX        0x0000000000010000UL

/** Link can run, should run, or is running in half-duplex mode.
 *  If neither ::GXIO_MPIPE_LINK_FDX nor ::GXIO_MPIPE_LINK_HDX are
 *  specified in a set of desired state flags, both are assumed. */
#define GXIO_MPIPE_LINK_HDX        0x0000000000020000UL


/** An individual rule. */
typedef struct
{
  /** The total size. */
  uint16_t size;

  /** The priority. */
  int16_t priority;

  /** The "headroom" in each buffer. */
  uint8_t headroom;

  /** The "tailroom" in each buffer. */
  uint8_t tailroom;

  /** The "capacity" of the largest buffer. */
  uint16_t capacity;

  /** The mask for converting a flow hash into a bucket. */
  uint16_t bucket_mask;

  /** The offset for converting a flow hash into a bucket. */
  uint16_t bucket_first;

  /** The buffer stack ids. */
  gxio_mpipe_rules_stacks_t stacks;

  /** The actual channels. */
  uint32_t channel_bits;

  /** The number of dmacs. */
  uint16_t num_dmacs;

  /** The number of vlans. */
  uint16_t num_vlans;

  /** The actual dmacs and vlans. */
  uint8_t dmacs_and_vlans[];

} gxio_mpipe_rules_rule_t;


/** A list of classifier rules. */
typedef struct
{
  /** The offset to the end of the current rule. */
  uint16_t tail;

  /** The offset to the start of the current rule. */
  uint16_t head;

  /** The actual rules. */
  uint8_t rules[4096 - 4];

} gxio_mpipe_rules_list_t;




/** mPIPE statistics structure. These counters include all relevant
 *  events occurring on all links within the mPIPE shim. */
typedef struct
{
  /** Number of ingress packets dropped for any reason. */
  uint64_t ingress_drops;
  /** Number of ingress packets dropped because a buffer stack was empty. */
  uint64_t ingress_drops_no_buf;
  /** Number of ingress packets dropped or truncated due to lack of space in
   *  the iPkt buffer. */
  uint64_t ingress_drops_ipkt;
  /** Number of ingress packets dropped by the classifier or load balancer */
  uint64_t ingress_drops_cls_lb;
  /** Total number of ingress packets. */
  uint64_t ingress_packets;
  /** Total number of egress packets. */
  uint64_t egress_packets;
  /** Total number of ingress bytes. */
  uint64_t ingress_bytes;
  /** Total number of egress bytes. */
  uint64_t egress_bytes;
}
gxio_mpipe_stats_t;


#endif /* _SYS_HV_DRV_MPIPE_INTF_H */
