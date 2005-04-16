/*
 *	PLX 9060 PCI Interface chip
 */

/*
 *	PCI configuration registers, same offset on local and PCI sides,
 *	but on PCI side, must use PCI BIOS calls to read/write.
 */
#define	PCI_PLXREGS_BASE_ADDR	0x10

#define	PCI_PLXREGS_IO_ADDR	0x14

#define	PCI_SPACE0_BASE_ADDR	0x18

#define	PCI_ROM_BASE_ADDR	0x30
#	define PCI_ROM_ENABLED		0x00000001

#define	PCI_INT_LINE		0x3C

/*
 *	Registers accessible directly from PCI and local side.
 *	Offset is from PCI side.  Add PLX_LCL_OFFSET for local address.
 */
#define	PLX_LCL_OFFSET	0x80	/* Offset of regs from local side */

/*
 *	Local Configuration Registers
 */
#define	PLX_SPACE0_RANGE	0x00	/* Range for PCI to Lcl Addr Space 0 */
#define	PLX_SPACE0_BASE_ADDR	0x04	/* Lcl Base address remap */

#define	PLX_ROM_RANGE		0x10	/* Range for expansion ROM (DMA) */
#define	PLX_ROM_BASE_ADDR	0x14	/* Lcl base address remap for ROM */

#define	PLX_BUS_REGION		0x18	/* Bus Region Descriptors */

/*
 *	Shared Run Time Registers
 */
#define	PLX_MBOX0		0x40
#define	PLX_MBOX1		0x44
#define	PLX_MBOX2		0x48
#define	PLX_MBOX3		0x4C
#define	PLX_MBOX4		0x50
#define	PLX_MBOX5		0x54
#define	PLX_MBOX6		0x58
#define	PLX_MBOX7		0x5C

#define	PLX_PCI2LCL_DOORBELL	0x60

#define	PLX_LCL2PCI_DOORBELL	0x64

#define	PLX_INT_CSR		0x68	/* Interrupt Control/Status */
#	define	PLX_LSERR_ENABLE	0x00000001
#	define	PLX_LSERR_PE		0x00000002
#	define	PLX_SERR		0x00000004
#	undef  PLX_UNUSED /*		0x00000008			*/
#	undef  PLX_UNUSED /*		0x00000010			*/
#	undef  PLX_UNUSED /*		0x00000020			*/
#	undef  PLX_UNUSED /*		0x00000040			*/
#	undef  PLX_UNUSED /*		0x00000080			*/
#	define PLX_PCI_IE		0x00000100
#	define	PLX_PCI_DOORBELL_IE	0x00000200
#	define	PLX_PCI_ABORT_IE	0x00000400
#	define	PLX_PCI_LOCAL_IE	0x00000800
#	define	PLX_RETRY_ABORT_ENABLE	0x00001000
#	define	PLX_PCI_DOORBELL_INT	0x00002000
#	define	PLX_PCI_ABORT_INT	0x00004000
#	define	PLX_PCI_LOCAL_INT	0x00008000
#	define	PLX_LCL_IE		0x00010000
#	define	PLX_LCL_DOORBELL_IE	0x00020000
#	define	PLX_LCL_DMA0_IE		0x00040000
#	define	PLX_LCL_DMA1_IE		0x00080000
#	define	PLX_LCL_DOORBELL_INT	0x00100000
#	define	PLX_LCL_DMA0_INT	0x00200000
#	define	PLX_LCL_DMA1_INT	0x00400000
#	define	PLX_LCL_BIST_INT	0x00800000
#	define	PLX_BM_DIRECT_		0x01000000
#	define	PLX_BM_DMA0_		0x02000000
#	define	PLX_BM_DMA1_		0x04000000
#	define	PLX_BM_ABORT_		0x08000000
#	undef  PLX_UNUSED /*		0x10000000			*/
#	undef  PLX_UNUSED /*		0x20000000			*/
#	undef  PLX_UNUSED /*		0x40000000			*/
#	undef  PLX_UNUSED /*		0x80000000			*/

#define	PLX_MISC_CSR		0x6c	/* EEPROM,PCI,User,Init Control/Status*/
#	define PLX_USEROUT		0x00010000
#	define PLX_USERIN		0x00020000
#	define PLX_EECK			0x01000000
#	define PLX_EECS			0x02000000
#	define PLX_EEWD			0x04000000
#	define PLX_EERD			0x08000000

/*
 *	DMA registers.  Offset is from local side
 */
#define	PLX_DMA0_MODE		0x100
#	define PLX_DMA_MODE_WIDTH32	0x00000003
#	define PLX_DMA_MODE_WAITSTATES(X)	((X)<<2)
#	define PLX_DMA_MODE_NOREADY	0x00000000
#	define PLX_DMA_MODE_READY	0x00000040
#	define PLX_DMA_MODE_NOBTERM	0x00000000
#	define PLX_DMA_MODE_BTERM	0x00000080
#	define PLX_DMA_MODE_NOBURST	0x00000000
#	define PLX_DMA_MODE_BURST	0x00000100
#	define PLX_DMA_MODE_NOCHAIN	0x00000000
#	define PLX_DMA_MODE_CHAIN	0x00000200
#	define PLX_DMA_MODE_DONE_IE	0x00000400
#	define PLX_DMA_MODE_ADDR_HOLD	0x00000800

#define	PLX_DMA0_PCI_ADDR	0x104
					/* non-chaining mode PCI address */

#define	PLX_DMA0_LCL_ADDR	0x108
					/* non-chaining mode local address */

#define	PLX_DMA0_SIZE		0x10C
					/* non-chaining mode length */

#define	PLX_DMA0_DESCRIPTOR	0x110
#	define	PLX_DMA_DESC_EOC	0x00000002
#	define	PLX_DMA_DESC_TC_IE	0x00000004
#	define	PLX_DMA_DESC_TO_HOST	0x00000008
#	define	PLX_DMA_DESC_TO_BOARD	0x00000000
#	define	PLX_DMA_DESC_NEXTADDR	0xFFFFfff0

#define	PLX_DMA1_MODE		0x114
#define	PLX_DMA1_PCI_ADDR	0x118
#define	PLX_DMA1_LCL_ADDR	0x11C
#define	PLX_DMA1_SIZE		0x110
#define	PLX_DMA1_DESCRIPTOR	0x124

#define	PLX_DMA_CSR		0x128
#	define PLX_DMA_CSR_0_ENABLE	0x00000001
#	define PLX_DMA_CSR_0_START	0x00000002
#	define PLX_DMA_CSR_0_ABORT	0x00000004
#	define PLX_DMA_CSR_0_CLR_INTR	0x00000008
#	define PLX_DMA_CSR_0_DONE	0x00000010
#	define PLX_DMA_CSR_1_ENABLE	0x00000100
#	define PLX_DMA_CSR_1_START	0x00000200
#	define PLX_DMA_CSR_1_ABORT	0x00000400
#	define PLX_DMA_CSR_1_CLR_INTR	0x00000800
#	define PLX_DMA_CSR_1_DONE	0x00001000

#define	PLX_DMA_ARB0		0x12C
#	define PLX_DMA_ARB0_LATENCY_T	0x000000FF
#	define PLX_DMA_ARB0_PAUSE_T	0x0000FF00
#	define PLX_DMA_ARB0_LATENCY_EN	0x00010000
#	define PLX_DMA_ARB0_PAUSE_EN	0x00020000
#	define PLX_DMA_ARB0_BREQ_EN	0x00040000
#	define PLX_DMA_ARB0_PRI		0x00180000
#	define PLX_DMA_ARB0_PRI_ROUND	0x00000000
#	define PLX_DMA_ARB0_PRI_0	0x00080000
#	define PLX_DMA_ARB0_PRI_1	0x00100000

#define	PLX_DMA_ARB1		0x130
						/* Chan 0: FIFO DEPTH=16 */
#	define PLX_DMA_ARB1_0_P2L_LW_TRIG(X)	( ((X)&15) <<  0 )
#	define PLX_DMA_ARB1_0_L2P_LR_TRIG(X)	( ((X)&15) <<  4 )
#	define PLX_DMA_ARB1_0_L2P_PW_TRIG(X)	( ((X)&15) <<  8 )
#	define PLX_DMA_ARB1_0_P2L_PR_TRIG(X)	( ((X)&15) << 12 )
						/* Chan 1: FIFO DEPTH=8 */
#	define PLX_DMA_ARB1_1_P2L_LW_TRIG(X)	( ((X)& 7) << 16 )
#	define PLX_DMA_ARB1_1_L2P_LR_TRIG(X)	( ((X)& 7) << 20 )
#	define PLX_DMA_ARB1_1_L2P_PW_TRIG(X)	( ((X)& 7) << 24 )
#	define PLX_DMA_ARB1_1_P2L_PR_TRIG(X)	( ((X)& 7) << 28 )

typedef struct _dmachain
{
	ulong		pciaddr;
	ulong		lcladdr;
	ulong		len;
	ulong		next;
} DMACHAIN;
