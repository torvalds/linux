#ifndef __CPM_H
#define __CPM_H

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/of.h>

/*
 * SPI Parameter RAM common to QE and CPM.
 */
struct spi_pram {
	__be16	rbase;	/* Rx Buffer descriptor base address */
	__be16	tbase;	/* Tx Buffer descriptor base address */
	u8	rfcr;	/* Rx function code */
	u8	tfcr;	/* Tx function code */
	__be16	mrblr;	/* Max receive buffer length */
	__be32	rstate;	/* Internal */
	__be32	rdp;	/* Internal */
	__be16	rbptr;	/* Internal */
	__be16	rbc;	/* Internal */
	__be32	rxtmp;	/* Internal */
	__be32	tstate;	/* Internal */
	__be32	tdp;	/* Internal */
	__be16	tbptr;	/* Internal */
	__be16	tbc;	/* Internal */
	__be32	txtmp;	/* Internal */
	__be32	res;	/* Tx temp. */
	__be16  rpbase;	/* Relocation pointer (CPM1 only) */
	__be16	res1;	/* Reserved */
};

/*
 * USB Controller pram common to QE and CPM.
 */
struct usb_ctlr {
	u8	usb_usmod;
	u8	usb_usadr;
	u8	usb_uscom;
	u8	res1[1];
	__be16	usb_usep[4];
	u8	res2[4];
	__be16	usb_usber;
	u8	res3[2];
	__be16	usb_usbmr;
	u8	res4[1];
	u8	usb_usbs;
	/* Fields down below are QE-only */
	__be16	usb_ussft;
	u8	res5[2];
	__be16	usb_usfrn;
	u8	res6[0x22];
} __attribute__ ((packed));

/*
 * Function code bits, usually generic to devices.
 */
#ifdef CONFIG_CPM1
#define CPMFCR_GBL	((u_char)0x00)	/* Flag doesn't exist in CPM1 */
#define CPMFCR_TC2	((u_char)0x00)	/* Flag doesn't exist in CPM1 */
#define CPMFCR_DTB	((u_char)0x00)	/* Flag doesn't exist in CPM1 */
#define CPMFCR_BDB	((u_char)0x00)	/* Flag doesn't exist in CPM1 */
#else
#define CPMFCR_GBL	((u_char)0x20)	/* Set memory snooping */
#define CPMFCR_TC2	((u_char)0x04)	/* Transfer code 2 value */
#define CPMFCR_DTB	((u_char)0x02)	/* Use local bus for data when set */
#define CPMFCR_BDB	((u_char)0x01)	/* Use local bus for BD when set */
#endif
#define CPMFCR_EB	((u_char)0x10)	/* Set big endian byte order */

/* Opcodes common to CPM1 and CPM2
*/
#define CPM_CR_INIT_TRX		((ushort)0x0000)
#define CPM_CR_INIT_RX		((ushort)0x0001)
#define CPM_CR_INIT_TX		((ushort)0x0002)
#define CPM_CR_HUNT_MODE	((ushort)0x0003)
#define CPM_CR_STOP_TX		((ushort)0x0004)
#define CPM_CR_GRA_STOP_TX	((ushort)0x0005)
#define CPM_CR_RESTART_TX	((ushort)0x0006)
#define CPM_CR_CLOSE_RX_BD	((ushort)0x0007)
#define CPM_CR_SET_GADDR	((ushort)0x0008)
#define CPM_CR_SET_TIMER	((ushort)0x0008)
#define CPM_CR_STOP_IDMA	((ushort)0x000b)

/* Buffer descriptors used by many of the CPM protocols. */
typedef struct cpm_buf_desc {
	ushort	cbd_sc;		/* Status and Control */
	ushort	cbd_datlen;	/* Data length in buffer */
	uint	cbd_bufaddr;	/* Buffer address in host memory */
} cbd_t;

/* Buffer descriptor control/status used by serial
 */

#define BD_SC_EMPTY	(0x8000)	/* Receive is empty */
#define BD_SC_READY	(0x8000)	/* Transmit is ready */
#define BD_SC_WRAP	(0x2000)	/* Last buffer descriptor */
#define BD_SC_INTRPT	(0x1000)	/* Interrupt on change */
#define BD_SC_LAST	(0x0800)	/* Last buffer in frame */
#define BD_SC_TC	(0x0400)	/* Transmit CRC */
#define BD_SC_CM	(0x0200)	/* Continous mode */
#define BD_SC_ID	(0x0100)	/* Rec'd too many idles */
#define BD_SC_P		(0x0100)	/* xmt preamble */
#define BD_SC_BR	(0x0020)	/* Break received */
#define BD_SC_FR	(0x0010)	/* Framing error */
#define BD_SC_PR	(0x0008)	/* Parity error */
#define BD_SC_NAK	(0x0004)	/* NAK - did not respond */
#define BD_SC_OV	(0x0002)	/* Overrun */
#define BD_SC_UN	(0x0002)	/* Underrun */
#define BD_SC_CD	(0x0001)	/* */
#define BD_SC_CL	(0x0001)	/* Collision */

/* Buffer descriptor control/status used by Ethernet receive.
 * Common to SCC and FCC.
 */
#define BD_ENET_RX_EMPTY	(0x8000)
#define BD_ENET_RX_WRAP		(0x2000)
#define BD_ENET_RX_INTR		(0x1000)
#define BD_ENET_RX_LAST		(0x0800)
#define BD_ENET_RX_FIRST	(0x0400)
#define BD_ENET_RX_MISS		(0x0100)
#define BD_ENET_RX_BC		(0x0080)	/* FCC Only */
#define BD_ENET_RX_MC		(0x0040)	/* FCC Only */
#define BD_ENET_RX_LG		(0x0020)
#define BD_ENET_RX_NO		(0x0010)
#define BD_ENET_RX_SH		(0x0008)
#define BD_ENET_RX_CR		(0x0004)
#define BD_ENET_RX_OV		(0x0002)
#define BD_ENET_RX_CL		(0x0001)
#define BD_ENET_RX_STATS	(0x01ff)	/* All status bits */

/* Buffer descriptor control/status used by Ethernet transmit.
 * Common to SCC and FCC.
 */
#define BD_ENET_TX_READY	(0x8000)
#define BD_ENET_TX_PAD		(0x4000)
#define BD_ENET_TX_WRAP		(0x2000)
#define BD_ENET_TX_INTR		(0x1000)
#define BD_ENET_TX_LAST		(0x0800)
#define BD_ENET_TX_TC		(0x0400)
#define BD_ENET_TX_DEF		(0x0200)
#define BD_ENET_TX_HB		(0x0100)
#define BD_ENET_TX_LC		(0x0080)
#define BD_ENET_TX_RL		(0x0040)
#define BD_ENET_TX_RCMASK	(0x003c)
#define BD_ENET_TX_UN		(0x0002)
#define BD_ENET_TX_CSL		(0x0001)
#define BD_ENET_TX_STATS	(0x03ff)	/* All status bits */

/* Buffer descriptor control/status used by Transparent mode SCC.
 */
#define BD_SCC_TX_LAST		(0x0800)

/* Buffer descriptor control/status used by I2C.
 */
#define BD_I2C_START		(0x0400)

int cpm_muram_init(void);

#if defined(CONFIG_CPM) || defined(CONFIG_QUICC_ENGINE)
unsigned long cpm_muram_alloc(unsigned long size, unsigned long align);
int cpm_muram_free(unsigned long offset);
unsigned long cpm_muram_alloc_fixed(unsigned long offset, unsigned long size);
void __iomem *cpm_muram_addr(unsigned long offset);
unsigned long cpm_muram_offset(void __iomem *addr);
dma_addr_t cpm_muram_dma(void __iomem *addr);
#else
static inline unsigned long cpm_muram_alloc(unsigned long size,
					    unsigned long align)
{
	return -ENOSYS;
}

static inline int cpm_muram_free(unsigned long offset)
{
	return -ENOSYS;
}

static inline unsigned long cpm_muram_alloc_fixed(unsigned long offset,
						  unsigned long size)
{
	return -ENOSYS;
}

static inline void __iomem *cpm_muram_addr(unsigned long offset)
{
	return NULL;
}

static inline unsigned long cpm_muram_offset(void __iomem *addr)
{
	return -ENOSYS;
}

static inline dma_addr_t cpm_muram_dma(void __iomem *addr)
{
	return 0;
}
#endif /* defined(CONFIG_CPM) || defined(CONFIG_QUICC_ENGINE) */

#ifdef CONFIG_CPM
int cpm_command(u32 command, u8 opcode);
#else
static inline int cpm_command(u32 command, u8 opcode)
{
	return -ENOSYS;
}
#endif /* CONFIG_CPM */

int cpm2_gpiochip_add32(struct device_node *np);

#endif
