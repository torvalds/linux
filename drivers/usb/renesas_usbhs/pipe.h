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
#ifndef RENESAS_USB_PIPE_H
#define RENESAS_USB_PIPE_H

#include "./common.h"

/*
 *	struct
 */
struct usbhs_pipe {
	u32 pipe_type;	/* USB_ENDPOINT_XFER_xxx */

	struct usbhs_priv *priv;

	u32 flags;
#define USBHS_PIPE_FLAGS_IS_USED		(1 << 0)
#define USBHS_PIPE_FLAGS_IS_DIR_IN		(1 << 1)

	void *mod_private;
};

struct usbhs_pipe_info {
	struct usbhs_pipe *pipe;
	int size;	/* array size of "pipe" */
	int bufnmb_last;	/* FIXME : driver needs good allocator */
};

/*
 * pipe list
 */
#define __usbhs_for_each_pipe(start, pos, info, i)	\
	for (i = start, pos = (info)->pipe;		\
	     i < (info)->size;				\
	     i++, pos = (info)->pipe + i)

#define usbhs_for_each_pipe(pos, priv, i)			\
	__usbhs_for_each_pipe(1, pos, &((priv)->pipe_info), i)

#define usbhs_for_each_pipe_with_dcp(pos, priv, i)		\
	__usbhs_for_each_pipe(0, pos, &((priv)->pipe_info), i)

/*
 * pipe module probe / remove
 */
int usbhs_pipe_probe(struct usbhs_priv *priv);
void usbhs_pipe_remove(struct usbhs_priv *priv);

/*
 * cfifo
 */
int usbhs_fifo_write(struct usbhs_pipe *pipe, u8 *buf, int len);
int usbhs_fifo_read(struct usbhs_pipe *pipe, u8 *buf, int len);
int usbhs_fifo_prepare_write(struct usbhs_pipe *pipe);
int usbhs_fifo_prepare_read(struct usbhs_pipe *pipe);

void usbhs_fifo_enable(struct usbhs_pipe *pipe);
void usbhs_fifo_disable(struct usbhs_pipe *pipe);
void usbhs_fifo_stall(struct usbhs_pipe *pipe);

void usbhs_fifo_send_terminator(struct usbhs_pipe *pipe);


/*
 * usb request
 */
void usbhs_usbreq_get_val(struct usbhs_priv *priv, struct usb_ctrlrequest *req);
void usbhs_usbreq_set_val(struct usbhs_priv *priv, struct usb_ctrlrequest *req);

/*
 * pipe control
 */
struct usbhs_pipe
*usbhs_pipe_malloc(struct usbhs_priv *priv,
		   const struct usb_endpoint_descriptor *desc);

int usbhs_pipe_is_dir_in(struct usbhs_pipe *pipe);
void usbhs_pipe_init(struct usbhs_priv *priv);
int usbhs_pipe_get_maxpacket(struct usbhs_pipe *pipe);
void usbhs_pipe_clear_sequence(struct usbhs_pipe *pipe);

#define usbhs_pipe_number(p)	(int)((p) - (p)->priv->pipe_info.pipe)

/*
 * dcp control
 */
struct usbhs_pipe *usbhs_dcp_malloc(struct usbhs_priv *priv);
void usbhs_dcp_control_transfer_done(struct usbhs_pipe *pipe);

#endif /* RENESAS_USB_PIPE_H */
