#ifndef __CPM_H
#define __CPM_H

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/of.h>

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
unsigned long cpm_muram_alloc(unsigned long size, unsigned long align);
int cpm_muram_free(unsigned long offset);
unsigned long cpm_muram_alloc_fixed(unsigned long offset, unsigned long size);
void __iomem *cpm_muram_addr(unsigned long offset);
unsigned long cpm_muram_offset(void __iomem *addr);
dma_addr_t cpm_muram_dma(void __iomem *addr);
int cpm_command(u32 command, u8 opcode);

int cpm2_gpiochip_add32(struct device_node *np);

#endif
