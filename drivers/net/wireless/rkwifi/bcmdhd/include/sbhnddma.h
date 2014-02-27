/*
 * Generic Broadcom Home Networking Division (HND) DMA engine HW interface
 * This supports the following chips: BCM42xx, 44xx, 47xx .
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: sbhnddma.h 373617 2012-12-07 23:03:08Z $
 */

#ifndef	_sbhnddma_h_
#define	_sbhnddma_h_







typedef volatile struct {
	uint32	control;		
	uint32	addr;			
	uint32	ptr;			
	uint32	status;			
} dma32regs_t;

typedef volatile struct {
	dma32regs_t	xmt;		
	dma32regs_t	rcv;		
} dma32regp_t;

typedef volatile struct {	
	uint32	fifoaddr;		
	uint32	fifodatalow;		
	uint32	fifodatahigh;		
	uint32	pad;			
} dma32diag_t;


typedef volatile struct {
	uint32	ctrl;		
	uint32	addr;		
} dma32dd_t;


#define	D32RINGALIGN_BITS	12
#define	D32MAXRINGSZ		(1 << D32RINGALIGN_BITS)
#define	D32RINGALIGN		(1 << D32RINGALIGN_BITS)

#define	D32MAXDD	(D32MAXRINGSZ / sizeof (dma32dd_t))


#define	XC_XE		((uint32)1 << 0)	
#define	XC_SE		((uint32)1 << 1)	
#define	XC_LE		((uint32)1 << 2)	
#define	XC_FL		((uint32)1 << 4)	
#define XC_MR_MASK	0x000000C0		
#define XC_MR_SHIFT	6
#define	XC_PD		((uint32)1 << 11)	
#define	XC_AE		((uint32)3 << 16)	
#define	XC_AE_SHIFT	16
#define XC_BL_MASK	0x001C0000		
#define XC_BL_SHIFT	18
#define XC_PC_MASK	0x00E00000		
#define XC_PC_SHIFT	21
#define XC_PT_MASK	0x03000000		
#define XC_PT_SHIFT	24


#define DMA_MR_1	0
#define DMA_MR_2	1



#define DMA_BL_16	0
#define DMA_BL_32	1
#define DMA_BL_64	2
#define DMA_BL_128	3
#define DMA_BL_256	4
#define DMA_BL_512	5
#define DMA_BL_1024	6


#define DMA_PC_0	0
#define DMA_PC_4	1
#define DMA_PC_8	2
#define DMA_PC_16	3



#define DMA_PT_1	0
#define DMA_PT_2	1
#define DMA_PT_4	2
#define DMA_PT_8	3


#define	XP_LD_MASK	0xfff			


#define	XS_CD_MASK	0x0fff			
#define	XS_XS_MASK	0xf000			
#define	XS_XS_SHIFT	12
#define	XS_XS_DISABLED	0x0000			
#define	XS_XS_ACTIVE	0x1000			
#define	XS_XS_IDLE	0x2000			
#define	XS_XS_STOPPED	0x3000			
#define	XS_XS_SUSP	0x4000			
#define	XS_XE_MASK	0xf0000			
#define	XS_XE_SHIFT	16
#define	XS_XE_NOERR	0x00000			
#define	XS_XE_DPE	0x10000			
#define	XS_XE_DFU	0x20000			
#define	XS_XE_BEBR	0x30000			
#define	XS_XE_BEDA	0x40000			
#define	XS_AD_MASK	0xfff00000		
#define	XS_AD_SHIFT	20


#define	RC_RE		((uint32)1 << 0)	
#define	RC_RO_MASK	0xfe			
#define	RC_RO_SHIFT	1
#define	RC_FM		((uint32)1 << 8)	
#define	RC_SH		((uint32)1 << 9)	
#define	RC_OC		((uint32)1 << 10)	
#define	RC_PD		((uint32)1 << 11)	
#define	RC_AE		((uint32)3 << 16)	
#define	RC_AE_SHIFT	16
#define RC_BL_MASK	0x001C0000		
#define RC_BL_SHIFT	18
#define RC_PC_MASK	0x00E00000		
#define RC_PC_SHIFT	21
#define RC_PT_MASK	0x03000000		
#define RC_PT_SHIFT	24


#define	RP_LD_MASK	0xfff			


#define	RS_CD_MASK	0x0fff			
#define	RS_RS_MASK	0xf000			
#define	RS_RS_SHIFT	12
#define	RS_RS_DISABLED	0x0000			
#define	RS_RS_ACTIVE	0x1000			
#define	RS_RS_IDLE	0x2000			
#define	RS_RS_STOPPED	0x3000			
#define	RS_RE_MASK	0xf0000			
#define	RS_RE_SHIFT	16
#define	RS_RE_NOERR	0x00000			
#define	RS_RE_DPE	0x10000			
#define	RS_RE_DFO	0x20000			
#define	RS_RE_BEBW	0x30000			
#define	RS_RE_BEDA	0x40000			
#define	RS_AD_MASK	0xfff00000		
#define	RS_AD_SHIFT	20


#define	FA_OFF_MASK	0xffff			
#define	FA_SEL_MASK	0xf0000			
#define	FA_SEL_SHIFT	16
#define	FA_SEL_XDD	0x00000			
#define	FA_SEL_XDP	0x10000			
#define	FA_SEL_RDD	0x40000			
#define	FA_SEL_RDP	0x50000			
#define	FA_SEL_XFD	0x80000			
#define	FA_SEL_XFP	0x90000			
#define	FA_SEL_RFD	0xc0000			
#define	FA_SEL_RFP	0xd0000			
#define	FA_SEL_RSD	0xe0000			
#define	FA_SEL_RSP	0xf0000			


#define	CTRL_BC_MASK	0x00001fff		
#define	CTRL_AE		((uint32)3 << 16)	
#define	CTRL_AE_SHIFT	16
#define	CTRL_PARITY	((uint32)3 << 18)	
#define	CTRL_EOT	((uint32)1 << 28)	
#define	CTRL_IOC	((uint32)1 << 29)	
#define	CTRL_EOF	((uint32)1 << 30)	
#define	CTRL_SOF	((uint32)1 << 31)	


#define	CTRL_CORE_MASK	0x0ff00000




typedef volatile struct {
	uint32	control;		
	uint32	ptr;			
	uint32	addrlow;		
	uint32	addrhigh;		
	uint32	status0;		
	uint32	status1;		
} dma64regs_t;

typedef volatile struct {
	dma64regs_t	tx;		
	dma64regs_t	rx;		
} dma64regp_t;

typedef volatile struct {		
	uint32	fifoaddr;		
	uint32	fifodatalow;		
	uint32	fifodatahigh;		
	uint32	pad;			
} dma64diag_t;


typedef volatile struct {
	uint32	ctrl1;		
	uint32	ctrl2;		
	uint32	addrlow;	
	uint32	addrhigh;	
} dma64dd_t;


#define D64RINGALIGN_BITS	13
#define	D64MAXRINGSZ		(1 << D64RINGALIGN_BITS)
#define	D64RINGBOUNDARY		(1 << D64RINGALIGN_BITS)

#define	D64MAXDD	(D64MAXRINGSZ / sizeof (dma64dd_t))


#define	D64MAXDD_LARGE		((1 << 16) / sizeof (dma64dd_t))


#define	D64RINGBOUNDARY_LARGE	(1 << 16)


#define D64_DEF_USBBURSTLEN     2
#define D64_DEF_SDIOBURSTLEN    1


#ifndef D64_USBBURSTLEN
#define D64_USBBURSTLEN	DMA_BL_64
#endif
#ifndef D64_SDIOBURSTLEN
#define D64_SDIOBURSTLEN	DMA_BL_32
#endif


#define	D64_XC_XE		0x00000001	
#define	D64_XC_SE		0x00000002	
#define	D64_XC_LE		0x00000004	
#define	D64_XC_FL		0x00000010	
#define D64_XC_MR_MASK		0x000000C0	
#define D64_XC_MR_SHIFT		6
#define	D64_XC_PD		0x00000800	
#define	D64_XC_AE		0x00030000	
#define	D64_XC_AE_SHIFT		16
#define D64_XC_BL_MASK		0x001C0000	
#define D64_XC_BL_SHIFT		18
#define D64_XC_PC_MASK		0x00E00000		
#define D64_XC_PC_SHIFT		21
#define D64_XC_PT_MASK		0x03000000		
#define D64_XC_PT_SHIFT		24


#define	D64_XP_LD_MASK		0x00001fff	


#define	D64_XS0_CD_MASK		(di->d64_xs0_cd_mask)	
#define	D64_XS0_XS_MASK		0xf0000000     	
#define	D64_XS0_XS_SHIFT		28
#define	D64_XS0_XS_DISABLED	0x00000000	
#define	D64_XS0_XS_ACTIVE	0x10000000	
#define	D64_XS0_XS_IDLE		0x20000000	
#define	D64_XS0_XS_STOPPED	0x30000000	
#define	D64_XS0_XS_SUSP		0x40000000	

#define	D64_XS1_AD_MASK		(di->d64_xs1_ad_mask)	
#define	D64_XS1_XE_MASK		0xf0000000     	
#define	D64_XS1_XE_SHIFT		28
#define	D64_XS1_XE_NOERR	0x00000000	
#define	D64_XS1_XE_DPE		0x10000000	
#define	D64_XS1_XE_DFU		0x20000000	
#define	D64_XS1_XE_DTE		0x30000000	
#define	D64_XS1_XE_DESRE	0x40000000	
#define	D64_XS1_XE_COREE	0x50000000	


#define	D64_RC_RE		0x00000001	
#define	D64_RC_RO_MASK		0x000000fe	
#define	D64_RC_RO_SHIFT		1
#define	D64_RC_FM		0x00000100	
#define	D64_RC_SH		0x00000200	
#define	D64_RC_OC		0x00000400	
#define	D64_RC_PD		0x00000800	
#define D64_RC_GE		0x00004000	
#define	D64_RC_AE		0x00030000	
#define	D64_RC_AE_SHIFT		16
#define D64_RC_BL_MASK		0x001C0000	
#define D64_RC_BL_SHIFT		18
#define D64_RC_PC_MASK		0x00E00000	
#define D64_RC_PC_SHIFT		21
#define D64_RC_PT_MASK		0x03000000	
#define D64_RC_PT_SHIFT		24


#define DMA_CTRL_PEN		(1 << 0)	
#define DMA_CTRL_ROC		(1 << 1)	
#define DMA_CTRL_RXMULTI	(1 << 2)	
#define DMA_CTRL_UNFRAMED	(1 << 3)	
#define DMA_CTRL_USB_BOUNDRY4KB_WAR (1 << 4)
#define DMA_CTRL_DMA_AVOIDANCE_WAR (1 << 5)	


#define	D64_RP_LD_MASK		0x00001fff	


#define	D64_RS0_CD_MASK		(di->d64_rs0_cd_mask)	
#define	D64_RS0_RS_MASK		0xf0000000     	
#define	D64_RS0_RS_SHIFT		28
#define	D64_RS0_RS_DISABLED	0x00000000	
#define	D64_RS0_RS_ACTIVE	0x10000000	
#define	D64_RS0_RS_IDLE		0x20000000	
#define	D64_RS0_RS_STOPPED	0x30000000	
#define	D64_RS0_RS_SUSP		0x40000000	

#define	D64_RS1_AD_MASK		0x0001ffff	
#define	D64_RS1_RE_MASK		0xf0000000     	
#define	D64_RS1_RE_SHIFT		28
#define	D64_RS1_RE_NOERR	0x00000000	
#define	D64_RS1_RE_DPO		0x10000000	
#define	D64_RS1_RE_DFU		0x20000000	
#define	D64_RS1_RE_DTE		0x30000000	
#define	D64_RS1_RE_DESRE	0x40000000	
#define	D64_RS1_RE_COREE	0x50000000	


#define	D64_FA_OFF_MASK		0xffff		
#define	D64_FA_SEL_MASK		0xf0000		
#define	D64_FA_SEL_SHIFT	16
#define	D64_FA_SEL_XDD		0x00000		
#define	D64_FA_SEL_XDP		0x10000		
#define	D64_FA_SEL_RDD		0x40000		
#define	D64_FA_SEL_RDP		0x50000		
#define	D64_FA_SEL_XFD		0x80000		
#define	D64_FA_SEL_XFP		0x90000		
#define	D64_FA_SEL_RFD		0xc0000		
#define	D64_FA_SEL_RFP		0xd0000		
#define	D64_FA_SEL_RSD		0xe0000		
#define	D64_FA_SEL_RSP		0xf0000		


#define D64_CTRL_COREFLAGS	0x0ff00000	
#define	D64_CTRL1_EOT		((uint32)1 << 28)	
#define	D64_CTRL1_IOC		((uint32)1 << 29)	
#define	D64_CTRL1_EOF		((uint32)1 << 30)	
#define	D64_CTRL1_SOF		((uint32)1 << 31)	


#define	D64_CTRL2_BC_MASK	0x00007fff	
#define	D64_CTRL2_AE		0x00030000	
#define	D64_CTRL2_AE_SHIFT	16
#define D64_CTRL2_PARITY	0x00040000      


#define	D64_CTRL_CORE_MASK	0x0ff00000

#define D64_RX_FRM_STS_LEN	0x0000ffff	
#define D64_RX_FRM_STS_OVFL	0x00800000	
#define D64_RX_FRM_STS_DSCRCNT	0x0f000000	
#define D64_RX_FRM_STS_DATATYPE	0xf0000000	


typedef volatile struct {
	uint16 len;
	uint16 flags;
} dma_rxh_t;

#endif	
