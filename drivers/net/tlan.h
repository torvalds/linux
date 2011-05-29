#ifndef TLAN_H
#define TLAN_H
/********************************************************************
 *
 *  Linux ThunderLAN Driver
 *
 *  tlan.h
 *  by James Banks
 *
 *  (C) 1997-1998 Caldera, Inc.
 *  (C) 1999-2001 Torben Mathiasen
 *
 *  This software may be used and distributed according to the terms
 *  of the GNU General Public License, incorporated herein by reference.
 *
 *
 *  Dec 10, 1999	Torben Mathiasen <torben.mathiasen@compaq.com>
 *			New Maintainer
 *
 ********************************************************************/


#include <linux/io.h>
#include <linux/types.h>
#include <linux/netdevice.h>



	/*****************************************************************
	 * TLan Definitions
	 *
	 ****************************************************************/

#define TLAN_MIN_FRAME_SIZE	64
#define TLAN_MAX_FRAME_SIZE	1600

#define TLAN_NUM_RX_LISTS	32
#define TLAN_NUM_TX_LISTS	64

#define TLAN_IGNORE		0
#define TLAN_RECORD		1

#define TLAN_DBG(lvl, format, args...)					\
	do {								\
		if (debug&lvl)						\
			printk(KERN_DEBUG "TLAN: " format, ##args);	\
	} while (0)

#define TLAN_DEBUG_GNRL		0x0001
#define TLAN_DEBUG_TX		0x0002
#define TLAN_DEBUG_RX		0x0004
#define TLAN_DEBUG_LIST		0x0008
#define TLAN_DEBUG_PROBE	0x0010

#define TX_TIMEOUT		(10*HZ)	 /* We need time for auto-neg */
#define MAX_TLAN_BOARDS		8	 /* Max number of boards installed
					    at a time */


	/*****************************************************************
	 * Device Identification Definitions
	 *
	 ****************************************************************/

#define PCI_DEVICE_ID_NETELLIGENT_10_T2			0xB012
#define PCI_DEVICE_ID_NETELLIGENT_10_100_WS_5100	0xB030
#ifndef PCI_DEVICE_ID_OLICOM_OC2183
#define PCI_DEVICE_ID_OLICOM_OC2183			0x0013
#endif
#ifndef PCI_DEVICE_ID_OLICOM_OC2325
#define PCI_DEVICE_ID_OLICOM_OC2325			0x0012
#endif
#ifndef PCI_DEVICE_ID_OLICOM_OC2326
#define PCI_DEVICE_ID_OLICOM_OC2326			0x0014
#endif

struct tlan_adapter_entry {
	u16	vendor_id;
	u16	device_id;
	char	*device_label;
	u32	flags;
	u16	addr_ofs;
};

#define TLAN_ADAPTER_NONE		0x00000000
#define TLAN_ADAPTER_UNMANAGED_PHY	0x00000001
#define TLAN_ADAPTER_BIT_RATE_PHY	0x00000002
#define TLAN_ADAPTER_USE_INTERN_10	0x00000004
#define TLAN_ADAPTER_ACTIVITY_LED	0x00000008

#define TLAN_SPEED_DEFAULT	0
#define TLAN_SPEED_10		10
#define TLAN_SPEED_100		100

#define TLAN_DUPLEX_DEFAULT	0
#define TLAN_DUPLEX_HALF	1
#define TLAN_DUPLEX_FULL	2



	/*****************************************************************
	 * EISA Definitions
	 *
	 ****************************************************************/

#define EISA_ID      0xc80   /* EISA ID Registers */
#define EISA_ID0     0xc80   /* EISA ID Register 0 */
#define EISA_ID1     0xc81   /* EISA ID Register 1 */
#define EISA_ID2     0xc82   /* EISA ID Register 2 */
#define EISA_ID3     0xc83   /* EISA ID Register 3 */
#define EISA_CR      0xc84   /* EISA Control Register */
#define EISA_REG0    0xc88   /* EISA Configuration Register 0 */
#define EISA_REG1    0xc89   /* EISA Configuration Register 1 */
#define EISA_REG2    0xc8a   /* EISA Configuration Register 2 */
#define EISA_REG3    0xc8f   /* EISA Configuration Register 3 */
#define EISA_APROM   0xc90   /* Ethernet Address PROM */



	/*****************************************************************
	 * Rx/Tx List Definitions
	 *
	 ****************************************************************/

#define TLAN_BUFFERS_PER_LIST	10
#define TLAN_LAST_BUFFER	0x80000000
#define TLAN_CSTAT_UNUSED	0x8000
#define TLAN_CSTAT_FRM_CMP	0x4000
#define TLAN_CSTAT_READY	0x3000
#define TLAN_CSTAT_EOC		0x0800
#define TLAN_CSTAT_RX_ERROR	0x0400
#define TLAN_CSTAT_PASS_CRC	0x0200
#define TLAN_CSTAT_DP_PR	0x0100


struct tlan_buffer {
	u32	count;
	u32	address;
};


struct tlan_list {
	u32		forward;
	u16		c_stat;
	u16		frame_size;
	struct tlan_buffer buffer[TLAN_BUFFERS_PER_LIST];
};


typedef u8 TLanBuffer[TLAN_MAX_FRAME_SIZE];




	/*****************************************************************
	 * PHY definitions
	 *
	 ****************************************************************/

#define TLAN_PHY_MAX_ADDR	0x1F
#define TLAN_PHY_NONE		0x20




	/*****************************************************************
	 * TLAN Private Information Structure
	 *
	 ****************************************************************/

struct tlan_priv {
	struct net_device       *next_device;
	struct pci_dev		*pci_dev;
	struct net_device       *dev;
	void			*dma_storage;
	dma_addr_t		dma_storage_dma;
	unsigned int		dma_size;
	u8			*pad_buffer;
	struct tlan_list	*rx_list;
	dma_addr_t		rx_list_dma;
	u8			*rx_buffer;
	dma_addr_t		rx_buffer_dma;
	u32			rx_head;
	u32			rx_tail;
	u32			rx_eoc_count;
	struct tlan_list	*tx_list;
	dma_addr_t		tx_list_dma;
	u8			*tx_buffer;
	dma_addr_t		tx_buffer_dma;
	u32			tx_head;
	u32			tx_in_progress;
	u32			tx_tail;
	u32			tx_busy_count;
	u32			phy_online;
	u32			timer_set_at;
	u32			timer_type;
	struct timer_list	timer;
	struct board		*adapter;
	u32			adapter_rev;
	u32			aui;
	u32			debug;
	u32			duplex;
	u32			phy[2];
	u32			phy_num;
	u32			speed;
	u8			tlan_rev;
	u8			tlan_full_duplex;
	spinlock_t		lock;
	u8			link;
	u8			is_eisa;
	struct work_struct			tlan_tqueue;
	u8			neg_be_verbose;
};




	/*****************************************************************
	 * TLan Driver Timer Definitions
	 *
	 ****************************************************************/

#define TLAN_TIMER_LINK_BEAT		1
#define TLAN_TIMER_ACTIVITY		2
#define TLAN_TIMER_PHY_PDOWN		3
#define TLAN_TIMER_PHY_PUP		4
#define TLAN_TIMER_PHY_RESET		5
#define TLAN_TIMER_PHY_START_LINK	6
#define TLAN_TIMER_PHY_FINISH_AN	7
#define TLAN_TIMER_FINISH_RESET		8

#define TLAN_TIMER_ACT_DELAY		(HZ/10)




	/*****************************************************************
	 * TLan Driver Eeprom Definitions
	 *
	 ****************************************************************/

#define TLAN_EEPROM_ACK		0
#define TLAN_EEPROM_STOP	1




	/*****************************************************************
	 * Host Register Offsets and Contents
	 *
	 ****************************************************************/

#define TLAN_HOST_CMD			0x00
#define	TLAN_HC_GO		0x80000000
#define		TLAN_HC_STOP		0x40000000
#define		TLAN_HC_ACK		0x20000000
#define		TLAN_HC_CS_MASK		0x1FE00000
#define		TLAN_HC_EOC		0x00100000
#define		TLAN_HC_RT		0x00080000
#define		TLAN_HC_NES		0x00040000
#define		TLAN_HC_AD_RST		0x00008000
#define		TLAN_HC_LD_TMR		0x00004000
#define		TLAN_HC_LD_THR		0x00002000
#define		TLAN_HC_REQ_INT		0x00001000
#define		TLAN_HC_INT_OFF		0x00000800
#define		TLAN_HC_INT_ON		0x00000400
#define		TLAN_HC_AC_MASK		0x000000FF
#define TLAN_CH_PARM			0x04
#define TLAN_DIO_ADR			0x08
#define		TLAN_DA_ADR_INC		0x8000
#define		TLAN_DA_RAM_ADR		0x4000
#define TLAN_HOST_INT			0x0A
#define		TLAN_HI_IV_MASK		0x1FE0
#define		TLAN_HI_IT_MASK		0x001C
#define TLAN_DIO_DATA			0x0C


/* ThunderLAN Internal Register DIO Offsets */

#define TLAN_NET_CMD			0x00
#define		TLAN_NET_CMD_NRESET	0x80
#define		TLAN_NET_CMD_NWRAP	0x40
#define		TLAN_NET_CMD_CSF	0x20
#define		TLAN_NET_CMD_CAF	0x10
#define		TLAN_NET_CMD_NOBRX	0x08
#define		TLAN_NET_CMD_DUPLEX	0x04
#define		TLAN_NET_CMD_TRFRAM	0x02
#define		TLAN_NET_CMD_TXPACE	0x01
#define TLAN_NET_SIO			0x01
#define	TLAN_NET_SIO_MINTEN	0x80
#define		TLAN_NET_SIO_ECLOK	0x40
#define		TLAN_NET_SIO_ETXEN	0x20
#define		TLAN_NET_SIO_EDATA	0x10
#define		TLAN_NET_SIO_NMRST	0x08
#define		TLAN_NET_SIO_MCLK	0x04
#define		TLAN_NET_SIO_MTXEN	0x02
#define		TLAN_NET_SIO_MDATA	0x01
#define TLAN_NET_STS			0x02
#define		TLAN_NET_STS_MIRQ	0x80
#define		TLAN_NET_STS_HBEAT	0x40
#define		TLAN_NET_STS_TXSTOP	0x20
#define		TLAN_NET_STS_RXSTOP	0x10
#define		TLAN_NET_STS_RSRVD	0x0F
#define TLAN_NET_MASK			0x03
#define		TLAN_NET_MASK_MASK7	0x80
#define		TLAN_NET_MASK_MASK6	0x40
#define		TLAN_NET_MASK_MASK5	0x20
#define		TLAN_NET_MASK_MASK4	0x10
#define		TLAN_NET_MASK_RSRVD	0x0F
#define TLAN_NET_CONFIG			0x04
#define	TLAN_NET_CFG_RCLK	0x8000
#define		TLAN_NET_CFG_TCLK	0x4000
#define		TLAN_NET_CFG_BIT	0x2000
#define		TLAN_NET_CFG_RXCRC	0x1000
#define		TLAN_NET_CFG_PEF	0x0800
#define		TLAN_NET_CFG_1FRAG	0x0400
#define		TLAN_NET_CFG_1CHAN	0x0200
#define		TLAN_NET_CFG_MTEST	0x0100
#define		TLAN_NET_CFG_PHY_EN	0x0080
#define		TLAN_NET_CFG_MSMASK	0x007F
#define TLAN_MAN_TEST			0x06
#define TLAN_DEF_VENDOR_ID		0x08
#define TLAN_DEF_DEVICE_ID		0x0A
#define TLAN_DEF_REVISION		0x0C
#define TLAN_DEF_SUBCLASS		0x0D
#define TLAN_DEF_MIN_LAT		0x0E
#define TLAN_DEF_MAX_LAT		0x0F
#define TLAN_AREG_0			0x10
#define TLAN_AREG_1			0x16
#define TLAN_AREG_2			0x1C
#define TLAN_AREG_3			0x22
#define TLAN_HASH_1			0x28
#define TLAN_HASH_2			0x2C
#define TLAN_GOOD_TX_FRMS		0x30
#define TLAN_TX_UNDERUNS		0x33
#define TLAN_GOOD_RX_FRMS		0x34
#define TLAN_RX_OVERRUNS		0x37
#define TLAN_DEFERRED_TX		0x38
#define TLAN_CRC_ERRORS			0x3A
#define TLAN_CODE_ERRORS		0x3B
#define TLAN_MULTICOL_FRMS		0x3C
#define TLAN_SINGLECOL_FRMS		0x3E
#define TLAN_EXCESSCOL_FRMS		0x40
#define TLAN_LATE_COLS			0x41
#define TLAN_CARRIER_LOSS		0x42
#define TLAN_ACOMMIT			0x43
#define TLAN_LED_REG			0x44
#define		TLAN_LED_ACT		0x10
#define		TLAN_LED_LINK		0x01
#define TLAN_BSIZE_REG			0x45
#define TLAN_MAX_RX			0x46
#define TLAN_INT_DIS			0x48
#define		TLAN_ID_TX_EOC		0x04
#define		TLAN_ID_RX_EOF		0x02
#define		TLAN_ID_RX_EOC		0x01



/* ThunderLAN Interrupt Codes */

#define TLAN_INT_NUMBER_OF_INTS	8

#define TLAN_INT_NONE			0x0000
#define TLAN_INT_TX_EOF			0x0001
#define TLAN_INT_STAT_OVERFLOW		0x0002
#define TLAN_INT_RX_EOF			0x0003
#define TLAN_INT_DUMMY			0x0004
#define TLAN_INT_TX_EOC			0x0005
#define TLAN_INT_STATUS_CHECK		0x0006
#define TLAN_INT_RX_EOC			0x0007



/* ThunderLAN MII Registers */

/* Generic MII/PHY Registers */

#define MII_GEN_CTL			0x00
#define	MII_GC_RESET		0x8000
#define		MII_GC_LOOPBK		0x4000
#define		MII_GC_SPEEDSEL		0x2000
#define		MII_GC_AUTOENB		0x1000
#define		MII_GC_PDOWN		0x0800
#define		MII_GC_ISOLATE		0x0400
#define		MII_GC_AUTORSRT		0x0200
#define		MII_GC_DUPLEX		0x0100
#define		MII_GC_COLTEST		0x0080
#define		MII_GC_RESERVED		0x007F
#define MII_GEN_STS			0x01
#define		MII_GS_100BT4		0x8000
#define		MII_GS_100BTXFD		0x4000
#define		MII_GS_100BTXHD		0x2000
#define		MII_GS_10BTFD		0x1000
#define		MII_GS_10BTHD		0x0800
#define		MII_GS_RESERVED		0x07C0
#define		MII_GS_AUTOCMPLT	0x0020
#define		MII_GS_RFLT		0x0010
#define		MII_GS_AUTONEG		0x0008
#define		MII_GS_LINK		0x0004
#define		MII_GS_JABBER		0x0002
#define		MII_GS_EXTCAP		0x0001
#define MII_GEN_ID_HI			0x02
#define MII_GEN_ID_LO			0x03
#define	MII_GIL_OUI		0xFC00
#define	MII_GIL_MODEL		0x03F0
#define	MII_GIL_REVISION	0x000F
#define MII_AN_ADV			0x04
#define MII_AN_LPA			0x05
#define MII_AN_EXP			0x06

/* ThunderLAN Specific MII/PHY Registers */

#define TLAN_TLPHY_ID			0x10
#define TLAN_TLPHY_CTL			0x11
#define	TLAN_TC_IGLINK		0x8000
#define		TLAN_TC_SWAPOL		0x4000
#define		TLAN_TC_AUISEL		0x2000
#define		TLAN_TC_SQEEN		0x1000
#define		TLAN_TC_MTEST		0x0800
#define		TLAN_TC_RESERVED	0x07F8
#define		TLAN_TC_NFEW		0x0004
#define		TLAN_TC_INTEN		0x0002
#define		TLAN_TC_TINT		0x0001
#define TLAN_TLPHY_STS			0x12
#define		TLAN_TS_MINT		0x8000
#define		TLAN_TS_PHOK		0x4000
#define		TLAN_TS_POLOK		0x2000
#define		TLAN_TS_TPENERGY	0x1000
#define		TLAN_TS_RESERVED	0x0FFF
#define TLAN_TLPHY_PAR			0x19
#define		TLAN_PHY_CIM_STAT	0x0020
#define		TLAN_PHY_SPEED_100	0x0040
#define		TLAN_PHY_DUPLEX_FULL	0x0080
#define		TLAN_PHY_AN_EN_STAT     0x0400

/* National Sem. & Level1 PHY id's */
#define NAT_SEM_ID1			0x2000
#define NAT_SEM_ID2			0x5C01
#define LEVEL1_ID1			0x7810
#define LEVEL1_ID2			0x0000

#define CIRC_INC(a, b) if (++a >= b) a = 0

/* Routines to access internal registers. */

static inline u8 tlan_dio_read8(u16 base_addr, u16 internal_addr)
{
	outw(internal_addr, base_addr + TLAN_DIO_ADR);
	return inb((base_addr + TLAN_DIO_DATA) + (internal_addr & 0x3));

}




static inline u16 tlan_dio_read16(u16 base_addr, u16 internal_addr)
{
	outw(internal_addr, base_addr + TLAN_DIO_ADR);
	return inw((base_addr + TLAN_DIO_DATA) + (internal_addr & 0x2));

}




static inline u32 tlan_dio_read32(u16 base_addr, u16 internal_addr)
{
	outw(internal_addr, base_addr + TLAN_DIO_ADR);
	return inl(base_addr + TLAN_DIO_DATA);

}




static inline void tlan_dio_write8(u16 base_addr, u16 internal_addr, u8 data)
{
	outw(internal_addr, base_addr + TLAN_DIO_ADR);
	outb(data, base_addr + TLAN_DIO_DATA + (internal_addr & 0x3));

}




static inline void tlan_dio_write16(u16 base_addr, u16 internal_addr, u16 data)
{
	outw(internal_addr, base_addr + TLAN_DIO_ADR);
	outw(data, base_addr + TLAN_DIO_DATA + (internal_addr & 0x2));

}




static inline void tlan_dio_write32(u16 base_addr, u16 internal_addr, u32 data)
{
	outw(internal_addr, base_addr + TLAN_DIO_ADR);
	outl(data, base_addr + TLAN_DIO_DATA + (internal_addr & 0x2));

}

#define tlan_clear_bit(bit, port)	outb_p(inb_p(port) & ~bit, port)
#define tlan_get_bit(bit, port)	((int) (inb_p(port) & bit))
#define tlan_set_bit(bit, port)	outb_p(inb_p(port) | bit, port)

/*
 * given 6 bytes, view them as 8 6-bit numbers and return the XOR of those
 * the code below is about seven times as fast as the original code
 *
 * The original code was:
 *
 * u32	xor(u32 a, u32 b) {	return ((a && !b ) || (! a && b )); }
 *
 * #define XOR8(a, b, c, d, e, f, g, h)	\
 *	xor(a, xor(b, xor(c, xor(d, xor(e, xor(f, xor(g, h)) ) ) ) ) )
 * #define DA(a, bit)		(( (u8) a[bit/8] ) & ( (u8) (1 << bit%8)) )
 *
 *	hash  = XOR8(DA(a,0), DA(a, 6), DA(a,12), DA(a,18), DA(a,24),
 *		      DA(a,30), DA(a,36), DA(a,42));
 *	hash |= XOR8(DA(a,1), DA(a, 7), DA(a,13), DA(a,19), DA(a,25),
 *		      DA(a,31), DA(a,37), DA(a,43)) << 1;
 *	hash |= XOR8(DA(a,2), DA(a, 8), DA(a,14), DA(a,20), DA(a,26),
 *		      DA(a,32), DA(a,38), DA(a,44)) << 2;
 *	hash |= XOR8(DA(a,3), DA(a, 9), DA(a,15), DA(a,21), DA(a,27),
 *		      DA(a,33), DA(a,39), DA(a,45)) << 3;
 *	hash |= XOR8(DA(a,4), DA(a,10), DA(a,16), DA(a,22), DA(a,28),
 *		      DA(a,34), DA(a,40), DA(a,46)) << 4;
 *	hash |= XOR8(DA(a,5), DA(a,11), DA(a,17), DA(a,23), DA(a,29),
 *		      DA(a,35), DA(a,41), DA(a,47)) << 5;
 *
 */
static inline u32 tlan_hash_func(const u8 *a)
{
	u8     hash;

	hash = (a[0]^a[3]);		/* & 077 */
	hash ^= ((a[0]^a[3])>>6);	/* & 003 */
	hash ^= ((a[1]^a[4])<<2);	/* & 074 */
	hash ^= ((a[1]^a[4])>>4);	/* & 017 */
	hash ^= ((a[2]^a[5])<<4);	/* & 060 */
	hash ^= ((a[2]^a[5])>>2);	/* & 077 */

	return hash & 077;
}
#endif
