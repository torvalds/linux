/*
 * Generic IEEE 1394 definitions
 */

#ifndef _IEEE1394_IEEE1394_H
#define _IEEE1394_IEEE1394_H

#define TCODE_WRITEQ             0x0
#define TCODE_WRITEB             0x1
#define TCODE_WRITE_RESPONSE     0x2
#define TCODE_READQ              0x4
#define TCODE_READB              0x5
#define TCODE_READQ_RESPONSE     0x6
#define TCODE_READB_RESPONSE     0x7
#define TCODE_CYCLE_START        0x8
#define TCODE_LOCK_REQUEST       0x9
#define TCODE_ISO_DATA           0xa
#define TCODE_STREAM_DATA        0xa
#define TCODE_LOCK_RESPONSE      0xb

#define RCODE_COMPLETE           0x0
#define RCODE_CONFLICT_ERROR     0x4
#define RCODE_DATA_ERROR         0x5
#define RCODE_TYPE_ERROR         0x6
#define RCODE_ADDRESS_ERROR      0x7

#define EXTCODE_MASK_SWAP        0x1
#define EXTCODE_COMPARE_SWAP     0x2
#define EXTCODE_FETCH_ADD        0x3
#define EXTCODE_LITTLE_ADD       0x4
#define EXTCODE_BOUNDED_ADD      0x5
#define EXTCODE_WRAP_ADD         0x6

#define ACK_COMPLETE             0x1
#define ACK_PENDING              0x2
#define ACK_BUSY_X               0x4
#define ACK_BUSY_A               0x5
#define ACK_BUSY_B               0x6
#define ACK_TARDY                0xb
#define ACK_CONFLICT_ERROR       0xc
#define ACK_DATA_ERROR           0xd
#define ACK_TYPE_ERROR           0xe
#define ACK_ADDRESS_ERROR        0xf

/* Non-standard "ACK codes" for internal use */
#define ACKX_NONE                (-1)
#define ACKX_SEND_ERROR          (-2)
#define ACKX_ABORTED             (-3)
#define ACKX_TIMEOUT             (-4)


#define IEEE1394_SPEED_100		0x00
#define IEEE1394_SPEED_200		0x01
#define IEEE1394_SPEED_400		0x02
#define IEEE1394_SPEED_800		0x03
#define IEEE1394_SPEED_1600		0x04
#define IEEE1394_SPEED_3200		0x05
/* The current highest tested speed supported by the subsystem */
#define IEEE1394_SPEED_MAX		IEEE1394_SPEED_800

/* Maps speed values above to a string representation */
extern const char *hpsb_speedto_str[];


/* 1394a cable PHY packets */
#define SELFID_PWRCL_NO_POWER    0x0
#define SELFID_PWRCL_PROVIDE_15W 0x1
#define SELFID_PWRCL_PROVIDE_30W 0x2
#define SELFID_PWRCL_PROVIDE_45W 0x3
#define SELFID_PWRCL_USE_1W      0x4
#define SELFID_PWRCL_USE_3W      0x5
#define SELFID_PWRCL_USE_6W      0x6
#define SELFID_PWRCL_USE_10W     0x7

#define SELFID_PORT_CHILD        0x3
#define SELFID_PORT_PARENT       0x2
#define SELFID_PORT_NCONN        0x1
#define SELFID_PORT_NONE         0x0

#define PHYPACKET_LINKON			0x40000000
#define PHYPACKET_PHYCONFIG_R			0x00800000
#define PHYPACKET_PHYCONFIG_T			0x00400000
#define EXTPHYPACKET_TYPE_PING			0x00000000
#define EXTPHYPACKET_TYPE_REMOTEACCESS_BASE	0x00040000
#define EXTPHYPACKET_TYPE_REMOTEACCESS_PAGED	0x00140000
#define EXTPHYPACKET_TYPE_REMOTEREPLY_BASE	0x000C0000
#define EXTPHYPACKET_TYPE_REMOTEREPLY_PAGED	0x001C0000
#define EXTPHYPACKET_TYPE_REMOTECOMMAND		0x00200000
#define EXTPHYPACKET_TYPE_REMOTECONFIRMATION	0x00280000
#define EXTPHYPACKET_TYPE_RESUME		0x003C0000

#define EXTPHYPACKET_TYPEMASK			0xC0FC0000

#define PHYPACKET_PORT_SHIFT     24
#define PHYPACKET_GAPCOUNT_SHIFT 16

/* 1394a PHY register map bitmasks */
#define PHY_00_PHYSICAL_ID       0xFC
#define PHY_00_R                 0x02 /* Root */
#define PHY_00_PS                0x01 /* Power Status*/
#define PHY_01_RHB               0x80 /* Root Hold-Off */
#define PHY_01_IBR               0x80 /* Initiate Bus Reset */
#define PHY_01_GAP_COUNT         0x3F
#define PHY_02_EXTENDED          0xE0 /* 0x7 for 1394a-compliant PHY */
#define PHY_02_TOTAL_PORTS       0x1F
#define PHY_03_MAX_SPEED         0xE0
#define PHY_03_DELAY             0x0F
#define PHY_04_LCTRL             0x80 /* Link Active Report Control */
#define PHY_04_CONTENDER         0x40
#define PHY_04_JITTER            0x38
#define PHY_04_PWR_CLASS         0x07 /* Power Class */
#define PHY_05_WATCHDOG          0x80
#define PHY_05_ISBR              0x40 /* Initiate Short Bus Reset */
#define PHY_05_LOOP              0x20 /* Loop Detect */
#define PHY_05_PWR_FAIL          0x10 /* Cable Power Failure Detect */
#define PHY_05_TIMEOUT           0x08 /* Arbitration State Machine Timeout */
#define PHY_05_PORT_EVENT        0x04 /* Port Event Detect */
#define PHY_05_ENAB_ACCEL        0x02 /* Enable Arbitration Acceleration */
#define PHY_05_ENAB_MULTI        0x01 /* Ena. Multispeed Packet Concatenation */

#include <asm/byteorder.h>

#ifdef __BIG_ENDIAN_BITFIELD

struct selfid {
        u32 packet_identifier:2; /* always binary 10 */
        u32 phy_id:6;
        /* byte */
        u32 extended:1; /* if true is struct ext_selfid */
        u32 link_active:1;
        u32 gap_count:6;
        /* byte */
        u32 speed:2;
        u32 phy_delay:2;
        u32 contender:1;
        u32 power_class:3;
        /* byte */
        u32 port0:2;
        u32 port1:2;
        u32 port2:2;
        u32 initiated_reset:1;
        u32 more_packets:1;
} __attribute__((packed));

struct ext_selfid {
        u32 packet_identifier:2; /* always binary 10 */
        u32 phy_id:6;
        /* byte */
        u32 extended:1; /* if false is struct selfid */
        u32 seq_nr:3;
        u32 reserved:2;
        u32 porta:2;
        /* byte */
        u32 portb:2;
        u32 portc:2;
        u32 portd:2;
        u32 porte:2;
        /* byte */
        u32 portf:2;
        u32 portg:2;
        u32 porth:2;
        u32 reserved2:1;
        u32 more_packets:1;
} __attribute__((packed));

#elif defined __LITTLE_ENDIAN_BITFIELD /* __BIG_ENDIAN_BITFIELD */

/*
 * Note: these mean to be bit fields of a big endian SelfID as seen on a little
 * endian machine.  Without swapping.
 */

struct selfid {
        u32 phy_id:6;
        u32 packet_identifier:2; /* always binary 10 */
        /* byte */
        u32 gap_count:6;
        u32 link_active:1;
        u32 extended:1; /* if true is struct ext_selfid */
        /* byte */
        u32 power_class:3;
        u32 contender:1;
        u32 phy_delay:2;
        u32 speed:2;
        /* byte */
        u32 more_packets:1;
        u32 initiated_reset:1;
        u32 port2:2;
        u32 port1:2;
        u32 port0:2;
} __attribute__((packed));

struct ext_selfid {
        u32 phy_id:6;
        u32 packet_identifier:2; /* always binary 10 */
        /* byte */
        u32 porta:2;
        u32 reserved:2;
        u32 seq_nr:3;
        u32 extended:1; /* if false is struct selfid */
        /* byte */
        u32 porte:2;
        u32 portd:2;
        u32 portc:2;
        u32 portb:2;
        /* byte */
        u32 more_packets:1;
        u32 reserved2:1;
        u32 porth:2;
        u32 portg:2;
        u32 portf:2;
} __attribute__((packed));

#else
#error What? PDP endian?
#endif /* __BIG_ENDIAN_BITFIELD */


#endif /* _IEEE1394_IEEE1394_H */
