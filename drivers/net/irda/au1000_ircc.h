/*
 *
 * BRIEF MODULE DESCRIPTION
 *	Au1000 IrDA driver.
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef AU1000_IRCC_H
#define AU1000_IRCC_H

#include <linux/time.h>

#include <linux/spinlock.h>
#include <linux/pm.h>
#include <asm/io.h>

#define NUM_IR_IFF          1
#define NUM_IR_DESC        64
#define RING_SIZE_4       0x0
#define RING_SIZE_16      0x3
#define RING_SIZE_64      0xF
#define MAX_NUM_IR_DESC    64
#define MAX_BUF_SIZE     2048

#define BPS_115200          0
#define BPS_57600           1
#define BPS_38400           2
#define BPS_19200           5
#define BPS_9600           11
#define BPS_2400           47

/* Ring descriptor flags */
#define AU_OWN           (1<<7) /* tx,rx */

#define IR_DIS_CRC       (1<<6) /* tx */
#define IR_BAD_CRC       (1<<5) /* tx */
#define IR_NEED_PULSE    (1<<4) /* tx */
#define IR_FORCE_UNDER   (1<<3) /* tx */
#define IR_DISABLE_TX    (1<<2) /* tx */
#define IR_HW_UNDER      (1<<0) /* tx */
#define IR_TX_ERROR      (IR_DIS_CRC|IR_BAD_CRC|IR_HW_UNDER)

#define IR_PHY_ERROR     (1<<6) /* rx */
#define IR_CRC_ERROR     (1<<5) /* rx */
#define IR_MAX_LEN       (1<<4) /* rx */
#define IR_FIFO_OVER     (1<<3) /* rx */
#define IR_SIR_ERROR     (1<<2) /* rx */
#define IR_RX_ERROR      (IR_PHY_ERROR|IR_CRC_ERROR| \
		IR_MAX_LEN|IR_FIFO_OVER|IR_SIR_ERROR)

typedef struct db_dest {
	struct db_dest *pnext;
	volatile u32 *vaddr;
	dma_addr_t dma_addr;
} db_dest_t;


typedef struct ring_desc {
	u8 count_0;               /* 7:0  */
	u8 count_1;               /* 12:8 */
	u8 reserved;
	u8 flags;
	u8 addr_0;                /* 7:0   */
	u8 addr_1;                /* 15:8  */
	u8 addr_2;                /* 23:16 */
	u8 addr_3;                /* 31:24 */
} ring_dest_t;


/* Private data for each instance */
struct au1k_private {

	db_dest_t *pDBfree;
	db_dest_t db[2*NUM_IR_DESC];
	volatile ring_dest_t *rx_ring[NUM_IR_DESC];
	volatile ring_dest_t *tx_ring[NUM_IR_DESC];
	db_dest_t *rx_db_inuse[NUM_IR_DESC];
	db_dest_t *tx_db_inuse[NUM_IR_DESC];
	u32 rx_head;
	u32 tx_head;
	u32 tx_tail;
	u32 tx_full;

	iobuff_t rx_buff;

	struct net_device *netdev;
	struct net_device_stats stats;
	
	struct timeval stamp;
	struct timeval now;
	struct qos_info		qos;
	struct irlap_cb		*irlap;
	
	u8 open;
	u32 speed;
	u32 newspeed;
	
	u32 intr_work_done; /* number of Rx and Tx pkts processed in the isr */
	struct timer_list timer;

	spinlock_t lock;           /* For serializing operations */
        struct pm_dev *dev;
};
#endif /* AU1000_IRCC_H */
