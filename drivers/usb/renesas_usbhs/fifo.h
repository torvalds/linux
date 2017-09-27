/*
 * Renesas USB driver
 *
 * Copyright (C) 2011 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#ifndef RENESAS_USB_FIFO_H
#define RENESAS_USB_FIFO_H

#include <linux/interrupt.h>
#include <linux/sh_dma.h>
#include <linux/workqueue.h>
#include <asm/dma.h>
#include "pipe.h"

struct usbhs_fifo {
	char *name;
	u32 port;	/* xFIFO */
	u32 sel;	/* xFIFOSEL */
	u32 ctr;	/* xFIFOCTR */

	struct usbhs_pipe	*pipe;

	struct dma_chan		*tx_chan;
	struct dma_chan		*rx_chan;

	struct sh_dmae_slave	tx_slave;
	struct sh_dmae_slave	rx_slave;
};

#define USBHS_MAX_NUM_DFIFO	4
struct usbhs_fifo_info {
	struct usbhs_fifo cfifo;
	struct usbhs_fifo dfifo[USBHS_MAX_NUM_DFIFO];
};
#define usbhsf_get_dnfifo(p, n)	(&((p)->fifo_info.dfifo[n]))
#define usbhs_for_each_dfifo(priv, dfifo, i)			\
	for ((i) = 0;						\
	     ((i) < USBHS_MAX_NUM_DFIFO) &&			\
		     ((dfifo) = usbhsf_get_dnfifo(priv, (i)));	\
	     (i)++)

struct usbhs_pkt_handle;
struct usbhs_pkt {
	struct list_head node;
	struct usbhs_pipe *pipe;
	const struct usbhs_pkt_handle *handler;
	void (*done)(struct usbhs_priv *priv,
		     struct usbhs_pkt *pkt);
	struct work_struct work;
	dma_addr_t dma;
	dma_cookie_t cookie;
	void *buf;
	int length;
	int trans;
	int actual;
	int zero;
	int sequence;
};

struct usbhs_pkt_handle {
	int (*prepare)(struct usbhs_pkt *pkt, int *is_done);
	int (*try_run)(struct usbhs_pkt *pkt, int *is_done);
	int (*dma_done)(struct usbhs_pkt *pkt, int *is_done);
};

/*
 * fifo
 */
int usbhs_fifo_probe(struct usbhs_priv *priv);
void usbhs_fifo_remove(struct usbhs_priv *priv);
void usbhs_fifo_init(struct usbhs_priv *priv);
void usbhs_fifo_quit(struct usbhs_priv *priv);
void usbhs_fifo_clear_dcp(struct usbhs_pipe *pipe);

/*
 * packet info
 */
extern const struct usbhs_pkt_handle usbhs_fifo_pio_push_handler;
extern const struct usbhs_pkt_handle usbhs_fifo_pio_pop_handler;
extern const struct usbhs_pkt_handle usbhs_ctrl_stage_end_handler;

extern const struct usbhs_pkt_handle usbhs_fifo_dma_push_handler;
extern const struct usbhs_pkt_handle usbhs_fifo_dma_pop_handler;

extern const struct usbhs_pkt_handle usbhs_dcp_status_stage_in_handler;
extern const struct usbhs_pkt_handle usbhs_dcp_status_stage_out_handler;

extern const struct usbhs_pkt_handle usbhs_dcp_data_stage_in_handler;
extern const struct usbhs_pkt_handle usbhs_dcp_data_stage_out_handler;

void usbhs_pkt_init(struct usbhs_pkt *pkt);
void usbhs_pkt_push(struct usbhs_pipe *pipe, struct usbhs_pkt *pkt,
		    void (*done)(struct usbhs_priv *priv,
				 struct usbhs_pkt *pkt),
		    void *buf, int len, int zero, int sequence);
struct usbhs_pkt *usbhs_pkt_pop(struct usbhs_pipe *pipe, struct usbhs_pkt *pkt);
void usbhs_pkt_start(struct usbhs_pipe *pipe);

#endif /* RENESAS_USB_FIFO_H */
