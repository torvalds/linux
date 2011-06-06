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

#include "pipe.h"

struct usbhs_pkt_handle;
struct usbhs_pkt {
	struct list_head node;
	struct usbhs_pipe *pipe;
	struct usbhs_pkt_handle *handler;
	void *buf;
	int length;
	int actual;
	int zero;
};

struct usbhs_pkt_handle {
	int (*prepare)(struct usbhs_pkt *pkt);
	int (*try_run)(struct usbhs_pkt *pkt);
};

/*
 * fifo
 */
void usbhs_fifo_init(struct usbhs_priv *priv);
void usbhs_fifo_quit(struct usbhs_priv *priv);

/*
 * packet info
 */
extern struct usbhs_pkt_handle usbhs_fifo_push_handler;
extern struct usbhs_pkt_handle usbhs_fifo_pop_handler;
extern struct usbhs_pkt_handle usbhs_ctrl_stage_end_handler;

void usbhs_pkt_init(struct usbhs_pkt *pkt);
void usbhs_pkt_push(struct usbhs_pipe *pipe, struct usbhs_pkt *pkt,
		    struct usbhs_pkt_handle *handler,
		    void *buf, int len, int zero);
void usbhs_pkt_pop(struct usbhs_pkt *pkt);
struct usbhs_pkt *usbhs_pkt_get(struct usbhs_pipe *pipe);

#define usbhs_pkt_start(p)	((p)->handler->prepare(p))
#define usbhs_pkt_run(p)	((p)->handler->try_run(p))

#endif /* RENESAS_USB_FIFO_H */
