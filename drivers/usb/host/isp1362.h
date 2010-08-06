/*
 * ISP1362 HCD (Host Controller Driver) for USB.
 *
 * COPYRIGHT (C) by L. Wassmann <LW@KARO-electronics.de>
 */

/* ------------------------------------------------------------------------- */
/*
 * Platform specific compile time options
 */
#if defined(CONFIG_ARCH_KARO)
#include <asm/arch/hardware.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/karo.h>

#define USE_32BIT		1


/* These options are mutually eclusive */
#define USE_PLATFORM_DELAY	1
#define USE_NDELAY		0
/*
 * MAX_ROOT_PORTS: Number of downstream ports
 *
 * The chip has two USB ports, one of which can be configured as
 * an USB device port, so the value of this constant is implementation
 * specific.
 */
#define MAX_ROOT_PORTS		2
#define DUMMY_DELAY_ACCESS do {} while (0)

/* insert platform specific definitions for other machines here */
#elif defined(CONFIG_BLACKFIN)

#include <linux/io.h>
#define USE_32BIT		0
#define MAX_ROOT_PORTS		2
#define USE_PLATFORM_DELAY	0
#define USE_NDELAY		1

#define DUMMY_DELAY_ACCESS \
	do { \
		bfin_read16(ASYNC_BANK0_BASE); \
		bfin_read16(ASYNC_BANK0_BASE); \
		bfin_read16(ASYNC_BANK0_BASE); \
	} while (0)

#undef insw
#undef outsw

#define insw  delayed_insw
#define outsw  delayed_outsw

static inline void delayed_outsw(unsigned int addr, void *buf, int len)
{
	unsigned short *bp = (unsigned short *)buf;
	while (len--) {
		DUMMY_DELAY_ACCESS;
		outw(*bp++, addr);
	}
}

static inline void delayed_insw(unsigned int addr, void *buf, int len)
{
	unsigned short *bp = (unsigned short *)buf;
	while (len--) {
		DUMMY_DELAY_ACCESS;
		*bp++ = inw(addr);
	}
}

#else

#define MAX_ROOT_PORTS		2

#define USE_32BIT		0

/* These options are mutually eclusive */
#define USE_PLATFORM_DELAY	0
#define USE_NDELAY		0

#define DUMMY_DELAY_ACCESS do {} while (0)

#endif


/* ------------------------------------------------------------------------- */

#define USB_RESET_WIDTH			50
#define MAX_XFER_SIZE			1023

/* Buffer sizes */
#define ISP1362_BUF_SIZE		4096
#define ISP1362_ISTL_BUFSIZE		512
#define ISP1362_INTL_BLKSIZE		64
#define ISP1362_INTL_BUFFERS		16
#define ISP1362_ATL_BLKSIZE		64

#define ISP1362_REG_WRITE_OFFSET	0x80

#ifdef ISP1362_DEBUG
typedef const unsigned int isp1362_reg_t;

#define REG_WIDTH_16			0x000
#define REG_WIDTH_32			0x100
#define REG_WIDTH_MASK			0x100
#define REG_NO_MASK			0x0ff

#define REG_ACCESS_R			0x200
#define REG_ACCESS_W			0x400
#define REG_ACCESS_RW			0x600
#define REG_ACCESS_MASK			0x600

#define ISP1362_REG_NO(r)		((r) & REG_NO_MASK)

#define _BUG_ON(x)	BUG_ON(x)
#define _WARN_ON(x)	WARN_ON(x)

#define ISP1362_REG(name, addr, width, rw) \
static isp1362_reg_t ISP1362_REG_##name = ((addr) | (width) | (rw))

#define REG_ACCESS_TEST(r)   BUG_ON(((r) & ISP1362_REG_WRITE_OFFSET) && !((r) & REG_ACCESS_W))
#define REG_WIDTH_TEST(r, w) BUG_ON(((r) & REG_WIDTH_MASK) != (w))
#else
typedef const unsigned char isp1362_reg_t;
#define ISP1362_REG_NO(r)		(r)
#define _BUG_ON(x)			do {} while (0)
#define _WARN_ON(x)			do {} while (0)

#define ISP1362_REG(name, addr, width, rw) \
static isp1362_reg_t ISP1362_REG_##name = addr

#define REG_ACCESS_TEST(r)		do {} while (0)
#define REG_WIDTH_TEST(r, w)		do {} while (0)
#endif

/* OHCI compatible registers */
/*
 * Note: Some of the ISP1362 'OHCI' registers implement only
 * a subset of the bits defined in the OHCI spec.
 *
 * Bitmasks for the individual bits of these registers are defined in "ohci.h"
 */
ISP1362_REG(HCREVISION,	0x00,	REG_WIDTH_32,	REG_ACCESS_R);
ISP1362_REG(HCCONTROL,	0x01,	REG_WIDTH_32,	REG_ACCESS_RW);
ISP1362_REG(HCCMDSTAT,	0x02,	REG_WIDTH_32,	REG_ACCESS_RW);
ISP1362_REG(HCINTSTAT,	0x03,	REG_WIDTH_32,	REG_ACCESS_RW);
ISP1362_REG(HCINTENB,	0x04,	REG_WIDTH_32,	REG_ACCESS_RW);
ISP1362_REG(HCINTDIS,	0x05,	REG_WIDTH_32,	REG_ACCESS_RW);
ISP1362_REG(HCFMINTVL,	0x0d,	REG_WIDTH_32,	REG_ACCESS_RW);
ISP1362_REG(HCFMREM,	0x0e,	REG_WIDTH_32,	REG_ACCESS_RW);
ISP1362_REG(HCFMNUM,	0x0f,	REG_WIDTH_32,	REG_ACCESS_RW);
ISP1362_REG(HCLSTHRESH,	0x11,	REG_WIDTH_32,	REG_ACCESS_RW);
ISP1362_REG(HCRHDESCA,	0x12,	REG_WIDTH_32,	REG_ACCESS_RW);
ISP1362_REG(HCRHDESCB,	0x13,	REG_WIDTH_32,	REG_ACCESS_RW);
ISP1362_REG(HCRHSTATUS,	0x14,	REG_WIDTH_32,	REG_ACCESS_RW);
ISP1362_REG(HCRHPORT1,	0x15,	REG_WIDTH_32,	REG_ACCESS_RW);
ISP1362_REG(HCRHPORT2,	0x16,	REG_WIDTH_32,	REG_ACCESS_RW);

/* Philips ISP1362 specific registers */
ISP1362_REG(HCHWCFG,	0x20,	REG_WIDTH_16,	REG_ACCESS_RW);
#define HCHWCFG_DISABLE_SUSPEND	(1 << 15)
#define HCHWCFG_GLOBAL_PWRDOWN	(1 << 14)
#define HCHWCFG_PULLDOWN_DS2	(1 << 13)
#define HCHWCFG_PULLDOWN_DS1	(1 << 12)
#define HCHWCFG_CLKNOTSTOP	(1 << 11)
#define HCHWCFG_ANALOG_OC	(1 << 10)
#define HCHWCFG_ONEINT		(1 << 9)
#define HCHWCFG_DACK_MODE	(1 << 8)
#define HCHWCFG_ONEDMA		(1 << 7)
#define HCHWCFG_DACK_POL	(1 << 6)
#define HCHWCFG_DREQ_POL	(1 << 5)
#define HCHWCFG_DBWIDTH_MASK	(0x03 << 3)
#define HCHWCFG_DBWIDTH(n)	(((n) << 3) & HCHWCFG_DBWIDTH_MASK)
#define HCHWCFG_INT_POL		(1 << 2)
#define HCHWCFG_INT_TRIGGER	(1 << 1)
#define HCHWCFG_INT_ENABLE	(1 << 0)

ISP1362_REG(HCDMACFG,	0x21,	REG_WIDTH_16,	REG_ACCESS_RW);
#define HCDMACFG_CTR_ENABLE	(1 << 7)
#define HCDMACFG_BURST_LEN_MASK	(0x03 << 5)
#define HCDMACFG_BURST_LEN(n)	(((n) << 5) & HCDMACFG_BURST_LEN_MASK)
#define HCDMACFG_BURST_LEN_1	HCDMACFG_BURST_LEN(0)
#define HCDMACFG_BURST_LEN_4	HCDMACFG_BURST_LEN(1)
#define HCDMACFG_BURST_LEN_8	HCDMACFG_BURST_LEN(2)
#define HCDMACFG_DMA_ENABLE	(1 << 4)
#define HCDMACFG_BUF_TYPE_MASK	(0x07 << 1)
#define HCDMACFG_BUF_TYPE(n)	(((n) << 1) & HCDMACFG_BUF_TYPE_MASK)
#define HCDMACFG_BUF_ISTL0	HCDMACFG_BUF_TYPE(0)
#define HCDMACFG_BUF_ISTL1	HCDMACFG_BUF_TYPE(1)
#define HCDMACFG_BUF_INTL	HCDMACFG_BUF_TYPE(2)
#define HCDMACFG_BUF_ATL	HCDMACFG_BUF_TYPE(3)
#define HCDMACFG_BUF_DIRECT	HCDMACFG_BUF_TYPE(4)
#define HCDMACFG_DMA_RW_SELECT	(1 << 0)

ISP1362_REG(HCXFERCTR,	0x22,	REG_WIDTH_16,	REG_ACCESS_RW);

ISP1362_REG(HCuPINT,	0x24,	REG_WIDTH_16,	REG_ACCESS_RW);
#define HCuPINT_SOF		(1 << 0)
#define HCuPINT_ISTL0		(1 << 1)
#define HCuPINT_ISTL1		(1 << 2)
#define HCuPINT_EOT		(1 << 3)
#define HCuPINT_OPR		(1 << 4)
#define HCuPINT_SUSP		(1 << 5)
#define HCuPINT_CLKRDY		(1 << 6)
#define HCuPINT_INTL		(1 << 7)
#define HCuPINT_ATL		(1 << 8)
#define HCuPINT_OTG		(1 << 9)

ISP1362_REG(HCuPINTENB,	0x25,	REG_WIDTH_16,	REG_ACCESS_RW);
/* same bit definitions apply as for HCuPINT */

ISP1362_REG(HCCHIPID,	0x27,	REG_WIDTH_16,	REG_ACCESS_R);
#define HCCHIPID_MASK		0xff00
#define HCCHIPID_MAGIC		0x3600

ISP1362_REG(HCSCRATCH,	0x28,	REG_WIDTH_16,	REG_ACCESS_RW);

ISP1362_REG(HCSWRES,	0x29,	REG_WIDTH_16,	REG_ACCESS_W);
#define HCSWRES_MAGIC		0x00f6

ISP1362_REG(HCBUFSTAT,	0x2c,	REG_WIDTH_16,	REG_ACCESS_RW);
#define HCBUFSTAT_ISTL0_FULL	(1 << 0)
#define HCBUFSTAT_ISTL1_FULL	(1 << 1)
#define HCBUFSTAT_INTL_ACTIVE	(1 << 2)
#define HCBUFSTAT_ATL_ACTIVE	(1 << 3)
#define HCBUFSTAT_RESET_HWPP	(1 << 4)
#define HCBUFSTAT_ISTL0_ACTIVE	(1 << 5)
#define HCBUFSTAT_ISTL1_ACTIVE	(1 << 6)
#define HCBUFSTAT_ISTL0_DONE	(1 << 8)
#define HCBUFSTAT_ISTL1_DONE	(1 << 9)
#define HCBUFSTAT_PAIRED_PTDPP	(1 << 10)

ISP1362_REG(HCDIRADDR,	0x32,	REG_WIDTH_32,	REG_ACCESS_RW);
#define HCDIRADDR_ADDR_MASK	0x0000ffff
#define HCDIRADDR_ADDR(n)	(((n) << 0) & HCDIRADDR_ADDR_MASK)
#define HCDIRADDR_COUNT_MASK	0xffff0000
#define HCDIRADDR_COUNT(n)	(((n) << 16) & HCDIRADDR_COUNT_MASK)
ISP1362_REG(HCDIRDATA,	0x45,	REG_WIDTH_16,	REG_ACCESS_RW);

ISP1362_REG(HCISTLBUFSZ, 0x30,	REG_WIDTH_16,	REG_ACCESS_RW);
ISP1362_REG(HCISTL0PORT, 0x40,	REG_WIDTH_16,	REG_ACCESS_RW);
ISP1362_REG(HCISTL1PORT, 0x42,	REG_WIDTH_16,	REG_ACCESS_RW);
ISP1362_REG(HCISTLRATE,	0x47,	REG_WIDTH_16,	REG_ACCESS_RW);

ISP1362_REG(HCINTLBUFSZ, 0x33,	REG_WIDTH_16,	REG_ACCESS_RW);
ISP1362_REG(HCINTLPORT,	0x43,	REG_WIDTH_16,	REG_ACCESS_RW);
ISP1362_REG(HCINTLBLKSZ, 0x53,	REG_WIDTH_16,	REG_ACCESS_RW);
ISP1362_REG(HCINTLDONE,	0x17,	REG_WIDTH_32,	REG_ACCESS_R);
ISP1362_REG(HCINTLSKIP,	0x18,	REG_WIDTH_32,	REG_ACCESS_RW);
ISP1362_REG(HCINTLLAST,	0x19,	REG_WIDTH_32,	REG_ACCESS_RW);
ISP1362_REG(HCINTLCURR,	0x1a,	REG_WIDTH_16,	REG_ACCESS_R);

ISP1362_REG(HCATLBUFSZ, 0x34,	REG_WIDTH_16,	REG_ACCESS_RW);
ISP1362_REG(HCATLPORT,	0x44,	REG_WIDTH_16,	REG_ACCESS_RW);
ISP1362_REG(HCATLBLKSZ, 0x54,	REG_WIDTH_16,	REG_ACCESS_RW);
ISP1362_REG(HCATLDONE,	0x1b,	REG_WIDTH_32,	REG_ACCESS_R);
ISP1362_REG(HCATLSKIP,	0x1c,	REG_WIDTH_32,	REG_ACCESS_RW);
ISP1362_REG(HCATLLAST,	0x1d,	REG_WIDTH_32,	REG_ACCESS_RW);
ISP1362_REG(HCATLCURR,	0x1e,	REG_WIDTH_16,	REG_ACCESS_R);

ISP1362_REG(HCATLDTC,	0x51,	REG_WIDTH_16,	REG_ACCESS_RW);
ISP1362_REG(HCATLDTCTO,	0x52,	REG_WIDTH_16,	REG_ACCESS_RW);


ISP1362_REG(OTGCONTROL,	0x62,	REG_WIDTH_16,	REG_ACCESS_RW);
ISP1362_REG(OTGSTATUS,	0x67,	REG_WIDTH_16,	REG_ACCESS_R);
ISP1362_REG(OTGINT,	0x68,	REG_WIDTH_16,	REG_ACCESS_RW);
ISP1362_REG(OTGINTENB,	0x69,	REG_WIDTH_16,	REG_ACCESS_RW);
ISP1362_REG(OTGTIMER,	0x6A,	REG_WIDTH_16,	REG_ACCESS_RW);
ISP1362_REG(OTGALTTMR,	0x6C,	REG_WIDTH_16,	REG_ACCESS_RW);

/* Philips transfer descriptor, cpu-endian */
struct ptd {
	u16 count;
#define	PTD_COUNT_MSK	(0x3ff << 0)
#define	PTD_TOGGLE_MSK	(1 << 10)
#define	PTD_ACTIVE_MSK	(1 << 11)
#define	PTD_CC_MSK	(0xf << 12)
	u16 mps;
#define	PTD_MPS_MSK	(0x3ff << 0)
#define	PTD_SPD_MSK	(1 << 10)
#define	PTD_LAST_MSK	(1 << 11)
#define	PTD_EP_MSK	(0xf << 12)
	u16 len;
#define	PTD_LEN_MSK	(0x3ff << 0)
#define	PTD_DIR_MSK	(3 << 10)
#define	PTD_DIR_SETUP	(0)
#define	PTD_DIR_OUT	(1)
#define	PTD_DIR_IN	(2)
	u16 faddr;
#define	PTD_FA_MSK	(0x7f << 0)
/* PTD Byte 7: [StartingFrame (if ISO PTD) | StartingFrame[0..4], PollingRate[0..2] (if INT PTD)] */
#define PTD_SF_ISO_MSK	(0xff << 8)
#define PTD_SF_INT_MSK	(0x1f << 8)
#define PTD_PR_MSK	(0x07 << 13)
} __attribute__ ((packed, aligned(2)));
#define PTD_HEADER_SIZE sizeof(struct ptd)

/* ------------------------------------------------------------------------- */
/* Copied from ohci.h: */
/*
 * Hardware transfer status codes -- CC from PTD
 */
#define PTD_CC_NOERROR      0x00
#define PTD_CC_CRC          0x01
#define PTD_CC_BITSTUFFING  0x02
#define PTD_CC_DATATOGGLEM  0x03
#define PTD_CC_STALL        0x04
#define PTD_DEVNOTRESP      0x05
#define PTD_PIDCHECKFAIL    0x06
#define PTD_UNEXPECTEDPID   0x07
#define PTD_DATAOVERRUN     0x08
#define PTD_DATAUNDERRUN    0x09
    /* 0x0A, 0x0B reserved for hardware */
#define PTD_BUFFEROVERRUN   0x0C
#define PTD_BUFFERUNDERRUN  0x0D
    /* 0x0E, 0x0F reserved for HCD */
#define PTD_NOTACCESSED     0x0F


/* map OHCI TD status codes (CC) to errno values */
static const int cc_to_error[16] = {
	/* No  Error  */               0,
	/* CRC Error  */               -EILSEQ,
	/* Bit Stuff  */               -EPROTO,
	/* Data Togg  */               -EILSEQ,
	/* Stall      */               -EPIPE,
	/* DevNotResp */               -ETIMEDOUT,
	/* PIDCheck   */               -EPROTO,
	/* UnExpPID   */               -EPROTO,
	/* DataOver   */               -EOVERFLOW,
	/* DataUnder  */               -EREMOTEIO,
	/* (for hw)   */               -EIO,
	/* (for hw)   */               -EIO,
	/* BufferOver */               -ECOMM,
	/* BuffUnder  */               -ENOSR,
	/* (for HCD)  */               -EALREADY,
	/* (for HCD)  */               -EALREADY
};


/*
 * HcControl (control) register masks
 */
#define OHCI_CTRL_HCFS	(3 << 6)	/* host controller functional state */
#define OHCI_CTRL_RWC	(1 << 9)	/* remote wakeup connected */
#define OHCI_CTRL_RWE	(1 << 10)	/* remote wakeup enable */

/* pre-shifted values for HCFS */
#	define OHCI_USB_RESET	(0 << 6)
#	define OHCI_USB_RESUME	(1 << 6)
#	define OHCI_USB_OPER	(2 << 6)
#	define OHCI_USB_SUSPEND	(3 << 6)

/*
 * HcCommandStatus (cmdstatus) register masks
 */
#define OHCI_HCR	(1 << 0)	/* host controller reset */
#define OHCI_SOC  	(3 << 16)	/* scheduling overrun count */

/*
 * masks used with interrupt registers:
 * HcInterruptStatus (intrstatus)
 * HcInterruptEnable (intrenable)
 * HcInterruptDisable (intrdisable)
 */
#define OHCI_INTR_SO	(1 << 0)	/* scheduling overrun */
#define OHCI_INTR_WDH	(1 << 1)	/* writeback of done_head */
#define OHCI_INTR_SF	(1 << 2)	/* start frame */
#define OHCI_INTR_RD	(1 << 3)	/* resume detect */
#define OHCI_INTR_UE	(1 << 4)	/* unrecoverable error */
#define OHCI_INTR_FNO	(1 << 5)	/* frame number overflow */
#define OHCI_INTR_RHSC	(1 << 6)	/* root hub status change */
#define OHCI_INTR_OC	(1 << 30)	/* ownership change */
#define OHCI_INTR_MIE	(1 << 31)	/* master interrupt enable */

/* roothub.portstatus [i] bits */
#define RH_PS_CCS            0x00000001   	/* current connect status */
#define RH_PS_PES            0x00000002   	/* port enable status*/
#define RH_PS_PSS            0x00000004   	/* port suspend status */
#define RH_PS_POCI           0x00000008   	/* port over current indicator */
#define RH_PS_PRS            0x00000010  	/* port reset status */
#define RH_PS_PPS            0x00000100   	/* port power status */
#define RH_PS_LSDA           0x00000200    	/* low speed device attached */
#define RH_PS_CSC            0x00010000 	/* connect status change */
#define RH_PS_PESC           0x00020000   	/* port enable status change */
#define RH_PS_PSSC           0x00040000    	/* port suspend status change */
#define RH_PS_OCIC           0x00080000    	/* over current indicator change */
#define RH_PS_PRSC           0x00100000   	/* port reset status change */

/* roothub.status bits */
#define RH_HS_LPS	     0x00000001		/* local power status */
#define RH_HS_OCI	     0x00000002		/* over current indicator */
#define RH_HS_DRWE	     0x00008000		/* device remote wakeup enable */
#define RH_HS_LPSC	     0x00010000		/* local power status change */
#define RH_HS_OCIC	     0x00020000		/* over current indicator change */
#define RH_HS_CRWE	     0x80000000		/* clear remote wakeup enable */

/* roothub.b masks */
#define RH_B_DR		0x0000ffff		/* device removable flags */
#define RH_B_PPCM	0xffff0000		/* port power control mask */

/* roothub.a masks */
#define	RH_A_NDP	(0xff << 0)		/* number of downstream ports */
#define	RH_A_PSM	(1 << 8)		/* power switching mode */
#define	RH_A_NPS	(1 << 9)		/* no power switching */
#define	RH_A_DT		(1 << 10)		/* device type (mbz) */
#define	RH_A_OCPM	(1 << 11)		/* over current protection mode */
#define	RH_A_NOCP	(1 << 12)		/* no over current protection */
#define	RH_A_POTPGT	(0xff << 24)		/* power on to power good time */

#define	FI			0x2edf		/* 12000 bits per frame (-1) */
#define	FSMP(fi) 		(0x7fff & ((6 * ((fi) - 210)) / 7))
#define LSTHRESH		0x628		/* lowspeed bit threshold */

/* ------------------------------------------------------------------------- */

/* PTD accessor macros. */
#define PTD_GET_COUNT(p)	(((p)->count & PTD_COUNT_MSK) >> 0)
#define PTD_COUNT(v)		(((v) << 0) & PTD_COUNT_MSK)
#define PTD_GET_TOGGLE(p)	(((p)->count & PTD_TOGGLE_MSK) >> 10)
#define PTD_TOGGLE(v)		(((v) << 10) & PTD_TOGGLE_MSK)
#define PTD_GET_ACTIVE(p)	(((p)->count & PTD_ACTIVE_MSK) >> 11)
#define PTD_ACTIVE(v)		(((v) << 11) & PTD_ACTIVE_MSK)
#define PTD_GET_CC(p)		(((p)->count & PTD_CC_MSK) >> 12)
#define PTD_CC(v)		(((v) << 12) & PTD_CC_MSK)
#define PTD_GET_MPS(p)		(((p)->mps & PTD_MPS_MSK) >> 0)
#define PTD_MPS(v)		(((v) << 0) & PTD_MPS_MSK)
#define PTD_GET_SPD(p)		(((p)->mps & PTD_SPD_MSK) >> 10)
#define PTD_SPD(v)		(((v) << 10) & PTD_SPD_MSK)
#define PTD_GET_LAST(p)		(((p)->mps & PTD_LAST_MSK) >> 11)
#define PTD_LAST(v)		(((v) << 11) & PTD_LAST_MSK)
#define PTD_GET_EP(p)		(((p)->mps & PTD_EP_MSK) >> 12)
#define PTD_EP(v)		(((v) << 12) & PTD_EP_MSK)
#define PTD_GET_LEN(p)		(((p)->len & PTD_LEN_MSK) >> 0)
#define PTD_LEN(v)		(((v) << 0) & PTD_LEN_MSK)
#define PTD_GET_DIR(p)		(((p)->len & PTD_DIR_MSK) >> 10)
#define PTD_DIR(v)		(((v) << 10) & PTD_DIR_MSK)
#define PTD_GET_FA(p)		(((p)->faddr & PTD_FA_MSK) >> 0)
#define PTD_FA(v)		(((v) << 0) & PTD_FA_MSK)
#define PTD_GET_SF_INT(p)	(((p)->faddr & PTD_SF_INT_MSK) >> 8)
#define PTD_SF_INT(v)		(((v) << 8) & PTD_SF_INT_MSK)
#define PTD_GET_SF_ISO(p)	(((p)->faddr & PTD_SF_ISO_MSK) >> 8)
#define PTD_SF_ISO(v)		(((v) << 8) & PTD_SF_ISO_MSK)
#define PTD_GET_PR(p)		(((p)->faddr & PTD_PR_MSK) >> 13)
#define PTD_PR(v)		(((v) << 13) & PTD_PR_MSK)

#define	LOG2_PERIODIC_SIZE	5	/* arbitrary; this matches OHCI */
#define	PERIODIC_SIZE		(1 << LOG2_PERIODIC_SIZE)

struct isp1362_ep {
	struct usb_host_endpoint *hep;
	struct usb_device	*udev;

	/* philips transfer descriptor */
	struct ptd		ptd;

	u8			maxpacket;
	u8			epnum;
	u8			nextpid;
	u16			error_count;
	u16			length;		/* of current packet */
	s16			ptd_offset;	/* buffer offset in ISP1362 where
						   PTD has been stored
						   (for access thru HCDIRDATA) */
	int			ptd_index;
	int num_ptds;
	void 			*data;		/* to databuf */
	/* queue of active EPs (the ones transmitted to the chip) */
	struct list_head	active;

	/* periodic schedule */
	u8			branch;
	u16			interval;
	u16			load;
	u16			last_iso;

	/* async schedule */
	struct list_head	schedule;	/* list of all EPs that need processing */
	struct list_head	remove_list;
	int			num_req;
};

struct isp1362_ep_queue {
	struct list_head	active;		/* list of PTDs currently processed by HC */
	atomic_t		finishing;
	unsigned long		buf_map;
	unsigned long		skip_map;
	int			free_ptd;
	u16			buf_start;
	u16			buf_size;
	u16			blk_size;	/* PTD buffer block size for ATL and INTL */
	u8			buf_count;
	u8			buf_avail;
	char			name[16];

	/* for statistical tracking */
	u8			stat_maxptds;	/* Max # of ptds seen simultaneously in fifo */
	u8			ptd_count;	/* number of ptds submitted to this queue */
};

struct isp1362_hcd {
	spinlock_t		lock;
	void __iomem		*addr_reg;
	void __iomem		*data_reg;

	struct isp1362_platform_data *board;

	struct proc_dir_entry	*pde;
	unsigned long		stat1, stat2, stat4, stat8, stat16;

	/* HC registers */
	u32			intenb;		/* "OHCI" interrupts */
	u16			irqenb;		/* uP interrupts */

	/* Root hub registers */
	u32			rhdesca;
	u32			rhdescb;
	u32			rhstatus;
	u32			rhport[MAX_ROOT_PORTS];
	unsigned long		next_statechange;

	/* HC control reg shadow copy */
	u32			hc_control;

	/* async schedule: control, bulk */
	struct list_head	async;

	/* periodic schedule: int */
	u16			load[PERIODIC_SIZE];
	struct list_head	periodic;
	u16			fmindex;

	/* periodic schedule: isochronous */
	struct list_head	isoc;
	unsigned int		istl_flip:1;
	unsigned int		irq_active:1;

	/* Schedules for the current frame */
	struct isp1362_ep_queue atl_queue;
	struct isp1362_ep_queue intl_queue;
	struct isp1362_ep_queue istl_queue[2];

	/* list of PTDs retrieved from HC */
	struct list_head	remove_list;
	enum {
		ISP1362_INT_SOF,
		ISP1362_INT_ISTL0,
		ISP1362_INT_ISTL1,
		ISP1362_INT_EOT,
		ISP1362_INT_OPR,
		ISP1362_INT_SUSP,
		ISP1362_INT_CLKRDY,
		ISP1362_INT_INTL,
		ISP1362_INT_ATL,
		ISP1362_INT_OTG,
		NUM_ISP1362_IRQS
	} IRQ_NAMES;
	unsigned int		irq_stat[NUM_ISP1362_IRQS];
	int			req_serial;
};

static inline const char *ISP1362_INT_NAME(int n)
{
	switch (n) {
	case ISP1362_INT_SOF:    return "SOF";
	case ISP1362_INT_ISTL0:  return "ISTL0";
	case ISP1362_INT_ISTL1:  return "ISTL1";
	case ISP1362_INT_EOT:    return "EOT";
	case ISP1362_INT_OPR:    return "OPR";
	case ISP1362_INT_SUSP:   return "SUSP";
	case ISP1362_INT_CLKRDY: return "CLKRDY";
	case ISP1362_INT_INTL:   return "INTL";
	case ISP1362_INT_ATL:    return "ATL";
	case ISP1362_INT_OTG:    return "OTG";
	default:                 return "unknown";
	}
}

static inline void ALIGNSTAT(struct isp1362_hcd *isp1362_hcd, void *ptr)
{
	unsigned long p = (unsigned long)ptr;
	if (!(p & 0xf))
		isp1362_hcd->stat16++;
	else if (!(p & 0x7))
		isp1362_hcd->stat8++;
	else if (!(p & 0x3))
		isp1362_hcd->stat4++;
	else if (!(p & 0x1))
		isp1362_hcd->stat2++;
	else
		isp1362_hcd->stat1++;
}

static inline struct isp1362_hcd *hcd_to_isp1362_hcd(struct usb_hcd *hcd)
{
	return (struct isp1362_hcd *) (hcd->hcd_priv);
}

static inline struct usb_hcd *isp1362_hcd_to_hcd(struct isp1362_hcd *isp1362_hcd)
{
	return container_of((void *)isp1362_hcd, struct usb_hcd, hcd_priv);
}

#define frame_before(f1, f2)	((s16)((u16)f1 - (u16)f2) < 0)

/*
 * ISP1362 HW Interface
 */

#ifdef ISP1362_DEBUG
#define DBG(level, fmt...) \
	do { \
		if (dbg_level > level) \
			pr_debug(fmt); \
	} while (0)
#define _DBG(level, fmt...)	\
	do { \
		if (dbg_level > level) \
			printk(fmt); \
	} while (0)
#else
#define DBG(fmt...)		do {} while (0)
#define _DBG DBG
#endif

#ifdef VERBOSE
#    define VDBG(fmt...)	DBG(3, fmt)
#else
#    define VDBG(fmt...)	do {} while (0)
#endif

#ifdef REGISTERS
#    define RDBG(fmt...)	DBG(1, fmt)
#else
#    define RDBG(fmt...)	do {} while (0)
#endif

#ifdef URB_TRACE
#define URB_DBG(fmt...)		DBG(0, fmt)
#else
#define URB_DBG(fmt...)		do {} while (0)
#endif


#if USE_PLATFORM_DELAY
#if USE_NDELAY
#error USE_PLATFORM_DELAY and USE_NDELAY defined simultaneously.
#endif
#define	isp1362_delay(h, d)	(h)->board->delay(isp1362_hcd_to_hcd(h)->self.controller, d)
#elif USE_NDELAY
#define	isp1362_delay(h, d)	ndelay(d)
#else
#define	isp1362_delay(h, d)	do {} while (0)
#endif

#define get_urb(ep) ({							\
	BUG_ON(list_empty(&ep->hep->urb_list));				\
	container_of(ep->hep->urb_list.next, struct urb, urb_list);	\
})

/* basic access functions for ISP1362 chip registers */
/* NOTE: The contents of the address pointer register cannot be read back! The driver must ensure,
 * that all register accesses are performed with interrupts disabled, since the interrupt
 * handler has no way of restoring the previous state.
 */
static void isp1362_write_addr(struct isp1362_hcd *isp1362_hcd, isp1362_reg_t reg)
{
	/*_BUG_ON((reg & ISP1362_REG_WRITE_OFFSET) && !(reg & REG_ACCESS_W));*/
	REG_ACCESS_TEST(reg);
	_BUG_ON(!irqs_disabled());
	DUMMY_DELAY_ACCESS;
	writew(ISP1362_REG_NO(reg), isp1362_hcd->addr_reg);
	DUMMY_DELAY_ACCESS;
	isp1362_delay(isp1362_hcd, 1);
}

static void isp1362_write_data16(struct isp1362_hcd *isp1362_hcd, u16 val)
{
	_BUG_ON(!irqs_disabled());
	DUMMY_DELAY_ACCESS;
	writew(val, isp1362_hcd->data_reg);
}

static u16 isp1362_read_data16(struct isp1362_hcd *isp1362_hcd)
{
	u16 val;

	_BUG_ON(!irqs_disabled());
	DUMMY_DELAY_ACCESS;
	val = readw(isp1362_hcd->data_reg);

	return val;
}

static void isp1362_write_data32(struct isp1362_hcd *isp1362_hcd, u32 val)
{
	_BUG_ON(!irqs_disabled());
#if USE_32BIT
	DUMMY_DELAY_ACCESS;
	writel(val, isp1362_hcd->data_reg);
#else
	DUMMY_DELAY_ACCESS;
	writew((u16)val, isp1362_hcd->data_reg);
	DUMMY_DELAY_ACCESS;
	writew(val >> 16, isp1362_hcd->data_reg);
#endif
}

static u32 isp1362_read_data32(struct isp1362_hcd *isp1362_hcd)
{
	u32 val;

	_BUG_ON(!irqs_disabled());
#if USE_32BIT
	DUMMY_DELAY_ACCESS;
	val = readl(isp1362_hcd->data_reg);
#else
	DUMMY_DELAY_ACCESS;
	val = (u32)readw(isp1362_hcd->data_reg);
	DUMMY_DELAY_ACCESS;
	val |= (u32)readw(isp1362_hcd->data_reg) << 16;
#endif
	return val;
}

/* use readsw/writesw to access the fifo whenever possible */
/* assume HCDIRDATA or XFERCTR & addr_reg have been set up */
static void isp1362_read_fifo(struct isp1362_hcd *isp1362_hcd, void *buf, u16 len)
{
	u8 *dp = buf;
	u16 data;

	if (!len)
		return;

	_BUG_ON(!irqs_disabled());

	RDBG("%s: Reading %d byte from fifo to mem @ %p\n", __func__, len, buf);
#if USE_32BIT
	if (len >= 4) {
		RDBG("%s: Using readsl for %d dwords\n", __func__, len >> 2);
		readsl(isp1362_hcd->data_reg, dp, len >> 2);
		dp += len & ~3;
		len &= 3;
	}
#endif
	if (len >= 2) {
		RDBG("%s: Using readsw for %d words\n", __func__, len >> 1);
		insw((unsigned long)isp1362_hcd->data_reg, dp, len >> 1);
		dp += len & ~1;
		len &= 1;
	}

	BUG_ON(len & ~1);
	if (len > 0) {
		data = isp1362_read_data16(isp1362_hcd);
		RDBG("%s: Reading trailing byte %02x to mem @ %08x\n", __func__,
		     (u8)data, (u32)dp);
		*dp = (u8)data;
	}
}

static void isp1362_write_fifo(struct isp1362_hcd *isp1362_hcd, void *buf, u16 len)
{
	u8 *dp = buf;
	u16 data;

	if (!len)
		return;

	if ((unsigned long)dp & 0x1) {
		/* not aligned */
		for (; len > 1; len -= 2) {
			data = *dp++;
			data |= *dp++ << 8;
			isp1362_write_data16(isp1362_hcd, data);
		}
		if (len)
			isp1362_write_data16(isp1362_hcd, *dp);
		return;
	}

	_BUG_ON(!irqs_disabled());

	RDBG("%s: Writing %d byte to fifo from memory @%p\n", __func__, len, buf);
#if USE_32BIT
	if (len >= 4) {
		RDBG("%s: Using writesl for %d dwords\n", __func__, len >> 2);
		writesl(isp1362_hcd->data_reg, dp, len >> 2);
		dp += len & ~3;
		len &= 3;
	}
#endif
	if (len >= 2) {
		RDBG("%s: Using writesw for %d words\n", __func__, len >> 1);
		outsw((unsigned long)isp1362_hcd->data_reg, dp, len >> 1);
		dp += len & ~1;
		len &= 1;
	}

	BUG_ON(len & ~1);
	if (len > 0) {
		/* finally write any trailing byte; we don't need to care
		 * about the high byte of the last word written
		 */
		data = (u16)*dp;
		RDBG("%s: Sending trailing byte %02x from mem @ %08x\n", __func__,
			data, (u32)dp);
		isp1362_write_data16(isp1362_hcd, data);
	}
}

#define isp1362_read_reg16(d, r)		({			\
	u16 __v;							\
	REG_WIDTH_TEST(ISP1362_REG_##r, REG_WIDTH_16);			\
	isp1362_write_addr(d, ISP1362_REG_##r);				\
	__v = isp1362_read_data16(d);					\
	RDBG("%s: Read %04x from %s[%02x]\n", __func__, __v, #r,	\
	     ISP1362_REG_NO(ISP1362_REG_##r));				\
	__v;								\
})

#define isp1362_read_reg32(d, r)		({			\
	u32 __v;							\
	REG_WIDTH_TEST(ISP1362_REG_##r, REG_WIDTH_32);			\
	isp1362_write_addr(d, ISP1362_REG_##r);				\
	__v = isp1362_read_data32(d);					\
	RDBG("%s: Read %08x from %s[%02x]\n", __func__, __v, #r,	\
	     ISP1362_REG_NO(ISP1362_REG_##r));				\
	__v;								\
})

#define isp1362_write_reg16(d, r, v)	{					\
	REG_WIDTH_TEST(ISP1362_REG_##r, REG_WIDTH_16);				\
	isp1362_write_addr(d, (ISP1362_REG_##r) | ISP1362_REG_WRITE_OFFSET);	\
	isp1362_write_data16(d, (u16)(v));					\
	RDBG("%s: Wrote %04x to %s[%02x]\n", __func__, (u16)(v), #r,	\
	     ISP1362_REG_NO(ISP1362_REG_##r));					\
}

#define isp1362_write_reg32(d, r, v)	{					\
	REG_WIDTH_TEST(ISP1362_REG_##r, REG_WIDTH_32);				\
	isp1362_write_addr(d, (ISP1362_REG_##r) | ISP1362_REG_WRITE_OFFSET);	\
	isp1362_write_data32(d, (u32)(v));					\
	RDBG("%s: Wrote %08x to %s[%02x]\n", __func__, (u32)(v), #r,	\
	     ISP1362_REG_NO(ISP1362_REG_##r));					\
}

#define isp1362_set_mask16(d, r, m) {			\
	u16 __v;					\
	__v = isp1362_read_reg16(d, r);			\
	if ((__v | m) != __v)				\
		isp1362_write_reg16(d, r, __v | m);	\
}

#define isp1362_clr_mask16(d, r, m) {			\
	u16 __v;					\
	__v = isp1362_read_reg16(d, r);			\
	if ((__v & ~m) != __v)			\
		isp1362_write_reg16(d, r, __v & ~m);	\
}

#define isp1362_set_mask32(d, r, m) {			\
	u32 __v;					\
	__v = isp1362_read_reg32(d, r);			\
	if ((__v | m) != __v)				\
		isp1362_write_reg32(d, r, __v | m);	\
}

#define isp1362_clr_mask32(d, r, m) {			\
	u32 __v;					\
	__v = isp1362_read_reg32(d, r);			\
	if ((__v & ~m) != __v)			\
		isp1362_write_reg32(d, r, __v & ~m);	\
}

#ifdef ISP1362_DEBUG
#define isp1362_show_reg(d, r) {								\
	if ((ISP1362_REG_##r & REG_WIDTH_MASK) == REG_WIDTH_32)			\
		DBG(0, "%-12s[%02x]: %08x\n", #r,					\
			ISP1362_REG_NO(ISP1362_REG_##r), isp1362_read_reg32(d, r));	\
	else									\
		DBG(0, "%-12s[%02x]:     %04x\n", #r,					\
			ISP1362_REG_NO(ISP1362_REG_##r), isp1362_read_reg16(d, r));	\
}
#else
#define isp1362_show_reg(d, r)	do {} while (0)
#endif

static void __attribute__((__unused__)) isp1362_show_regs(struct isp1362_hcd *isp1362_hcd)
{
	isp1362_show_reg(isp1362_hcd, HCREVISION);
	isp1362_show_reg(isp1362_hcd, HCCONTROL);
	isp1362_show_reg(isp1362_hcd, HCCMDSTAT);
	isp1362_show_reg(isp1362_hcd, HCINTSTAT);
	isp1362_show_reg(isp1362_hcd, HCINTENB);
	isp1362_show_reg(isp1362_hcd, HCFMINTVL);
	isp1362_show_reg(isp1362_hcd, HCFMREM);
	isp1362_show_reg(isp1362_hcd, HCFMNUM);
	isp1362_show_reg(isp1362_hcd, HCLSTHRESH);
	isp1362_show_reg(isp1362_hcd, HCRHDESCA);
	isp1362_show_reg(isp1362_hcd, HCRHDESCB);
	isp1362_show_reg(isp1362_hcd, HCRHSTATUS);
	isp1362_show_reg(isp1362_hcd, HCRHPORT1);
	isp1362_show_reg(isp1362_hcd, HCRHPORT2);

	isp1362_show_reg(isp1362_hcd, HCHWCFG);
	isp1362_show_reg(isp1362_hcd, HCDMACFG);
	isp1362_show_reg(isp1362_hcd, HCXFERCTR);
	isp1362_show_reg(isp1362_hcd, HCuPINT);

	if (in_interrupt())
		DBG(0, "%-12s[%02x]:     %04x\n", "HCuPINTENB",
			 ISP1362_REG_NO(ISP1362_REG_HCuPINTENB), isp1362_hcd->irqenb);
	else
		isp1362_show_reg(isp1362_hcd, HCuPINTENB);
	isp1362_show_reg(isp1362_hcd, HCCHIPID);
	isp1362_show_reg(isp1362_hcd, HCSCRATCH);
	isp1362_show_reg(isp1362_hcd, HCBUFSTAT);
	isp1362_show_reg(isp1362_hcd, HCDIRADDR);
	/* Access would advance fifo
	 * isp1362_show_reg(isp1362_hcd, HCDIRDATA);
	 */
	isp1362_show_reg(isp1362_hcd, HCISTLBUFSZ);
	isp1362_show_reg(isp1362_hcd, HCISTLRATE);
	isp1362_show_reg(isp1362_hcd, HCINTLBUFSZ);
	isp1362_show_reg(isp1362_hcd, HCINTLBLKSZ);
	isp1362_show_reg(isp1362_hcd, HCINTLDONE);
	isp1362_show_reg(isp1362_hcd, HCINTLSKIP);
	isp1362_show_reg(isp1362_hcd, HCINTLLAST);
	isp1362_show_reg(isp1362_hcd, HCINTLCURR);
	isp1362_show_reg(isp1362_hcd, HCATLBUFSZ);
	isp1362_show_reg(isp1362_hcd, HCATLBLKSZ);
	/* only valid after ATL_DONE interrupt
	 * isp1362_show_reg(isp1362_hcd, HCATLDONE);
	 */
	isp1362_show_reg(isp1362_hcd, HCATLSKIP);
	isp1362_show_reg(isp1362_hcd, HCATLLAST);
	isp1362_show_reg(isp1362_hcd, HCATLCURR);
	isp1362_show_reg(isp1362_hcd, HCATLDTC);
	isp1362_show_reg(isp1362_hcd, HCATLDTCTO);
}

static void isp1362_write_diraddr(struct isp1362_hcd *isp1362_hcd, u16 offset, u16 len)
{
	_BUG_ON(offset & 1);
	_BUG_ON(offset >= ISP1362_BUF_SIZE);
	_BUG_ON(len > ISP1362_BUF_SIZE);
	_BUG_ON(offset + len > ISP1362_BUF_SIZE);
	len = (len + 1) & ~1;

	isp1362_clr_mask16(isp1362_hcd, HCDMACFG, HCDMACFG_CTR_ENABLE);
	isp1362_write_reg32(isp1362_hcd, HCDIRADDR,
			    HCDIRADDR_ADDR(offset) | HCDIRADDR_COUNT(len));
}

static void isp1362_read_buffer(struct isp1362_hcd *isp1362_hcd, void *buf, u16 offset, int len)
{
	_BUG_ON(offset & 1);

	isp1362_write_diraddr(isp1362_hcd, offset, len);

	DBG(3, "%s: Reading %d byte from buffer @%04x to memory @ %p\n",
	    __func__, len, offset, buf);

	isp1362_write_reg16(isp1362_hcd, HCuPINT, HCuPINT_EOT);
	_WARN_ON((isp1362_read_reg16(isp1362_hcd, HCuPINT) & HCuPINT_EOT));

	isp1362_write_addr(isp1362_hcd, ISP1362_REG_HCDIRDATA);

	isp1362_read_fifo(isp1362_hcd, buf, len);
	_WARN_ON(!(isp1362_read_reg16(isp1362_hcd, HCuPINT) & HCuPINT_EOT));
	isp1362_write_reg16(isp1362_hcd, HCuPINT, HCuPINT_EOT);
	_WARN_ON((isp1362_read_reg16(isp1362_hcd, HCuPINT) & HCuPINT_EOT));
}

static void isp1362_write_buffer(struct isp1362_hcd *isp1362_hcd, void *buf, u16 offset, int len)
{
	_BUG_ON(offset & 1);

	isp1362_write_diraddr(isp1362_hcd, offset, len);

	DBG(3, "%s: Writing %d byte to buffer @%04x from memory @ %p\n",
	    __func__, len, offset, buf);

	isp1362_write_reg16(isp1362_hcd, HCuPINT, HCuPINT_EOT);
	_WARN_ON((isp1362_read_reg16(isp1362_hcd, HCuPINT) & HCuPINT_EOT));

	isp1362_write_addr(isp1362_hcd, ISP1362_REG_HCDIRDATA | ISP1362_REG_WRITE_OFFSET);
	isp1362_write_fifo(isp1362_hcd, buf, len);

	_WARN_ON(!(isp1362_read_reg16(isp1362_hcd, HCuPINT) & HCuPINT_EOT));
	isp1362_write_reg16(isp1362_hcd, HCuPINT, HCuPINT_EOT);
	_WARN_ON((isp1362_read_reg16(isp1362_hcd, HCuPINT) & HCuPINT_EOT));
}

static void __attribute__((unused)) dump_data(char *buf, int len)
{
	if (dbg_level > 0) {
		int k;
		int lf = 0;

		for (k = 0; k < len; ++k) {
			if (!lf)
				DBG(0, "%04x:", k);
			printk(" %02x", ((u8 *) buf)[k]);
			lf = 1;
			if (!k)
				continue;
			if (k % 16 == 15) {
				printk("\n");
				lf = 0;
				continue;
			}
			if (k % 8 == 7)
				printk(" ");
			if (k % 4 == 3)
				printk(" ");
		}
		if (lf)
			printk("\n");
	}
}

#if defined(ISP1362_DEBUG) && defined(PTD_TRACE)

static void dump_ptd(struct ptd *ptd)
{
	DBG(0, "EP %p: CC=%x EP=%d DIR=%x CNT=%d LEN=%d MPS=%d TGL=%x ACT=%x FA=%d SPD=%x SF=%x PR=%x LST=%x\n",
	    container_of(ptd, struct isp1362_ep, ptd),
	    PTD_GET_CC(ptd), PTD_GET_EP(ptd), PTD_GET_DIR(ptd),
	    PTD_GET_COUNT(ptd), PTD_GET_LEN(ptd), PTD_GET_MPS(ptd),
	    PTD_GET_TOGGLE(ptd), PTD_GET_ACTIVE(ptd), PTD_GET_FA(ptd),
	    PTD_GET_SPD(ptd), PTD_GET_SF_INT(ptd), PTD_GET_PR(ptd), PTD_GET_LAST(ptd));
	DBG(0, "  %04x %04x %04x %04x\n", ptd->count, ptd->mps, ptd->len, ptd->faddr);
}

static void dump_ptd_out_data(struct ptd *ptd, u8 *buf)
{
	if (dbg_level > 0) {
		if (PTD_GET_DIR(ptd) != PTD_DIR_IN && PTD_GET_LEN(ptd)) {
			DBG(0, "--out->\n");
			dump_data(buf, PTD_GET_LEN(ptd));
		}
	}
}

static void dump_ptd_in_data(struct ptd *ptd, u8 *buf)
{
	if (dbg_level > 0) {
		if (PTD_GET_DIR(ptd) == PTD_DIR_IN && PTD_GET_COUNT(ptd)) {
			DBG(0, "<--in--\n");
			dump_data(buf, PTD_GET_COUNT(ptd));
		}
		DBG(0, "-----\n");
	}
}

static void dump_ptd_queue(struct isp1362_ep_queue *epq)
{
	struct isp1362_ep *ep;
	int dbg = dbg_level;

	dbg_level = 1;
	list_for_each_entry(ep, &epq->active, active) {
		dump_ptd(&ep->ptd);
		dump_data(ep->data, ep->length);
	}
	dbg_level = dbg;
}
#else
#define dump_ptd(ptd)			do {} while (0)
#define dump_ptd_in_data(ptd, buf)	do {} while (0)
#define dump_ptd_out_data(ptd, buf)	do {} while (0)
#define dump_ptd_data(ptd, buf)		do {} while (0)
#define dump_ptd_queue(epq)		do {} while (0)
#endif
