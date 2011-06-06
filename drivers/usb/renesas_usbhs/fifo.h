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

#include "common.h"

/*
 * fifo
 */
int usbhs_fifo_write(struct usbhs_pipe *pipe, u8 *buf, int len);
int usbhs_fifo_read(struct usbhs_pipe *pipe, u8 *buf, int len);
int usbhs_fifo_prepare_write(struct usbhs_pipe *pipe);
int usbhs_fifo_prepare_read(struct usbhs_pipe *pipe);

#endif /* RENESAS_USB_FIFO_H */
