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

struct usbhs_pkt {
	struct list_head node;
	struct usbhs_pipe *pipe;
	void *buf;
	int length;
	int actual;
	int zero;
};

/*
 * fifo
 */
int usbhs_fifo_write(struct usbhs_pkt *pkt);
int usbhs_fifo_read(struct usbhs_pkt *pkt);
int usbhs_fifo_prepare_write(struct usbhs_pipe *pipe);
int usbhs_fifo_prepare_read(struct usbhs_pipe *pipe);

/*
 * packet info
 */
void usbhs_pkt_init(struct usbhs_pkt *pkt);
void usbhs_pkt_push(struct usbhs_pipe *pipe, struct usbhs_pkt *pkt,
		    void *buf, int len, int zero);
void usbhs_pkt_pop(struct usbhs_pkt *pkt);
struct usbhs_pkt *usbhs_pkt_get(struct usbhs_pipe *pipe);

#endif /* RENESAS_USB_FIFO_H */
