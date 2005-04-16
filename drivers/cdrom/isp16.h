/* -- isp16.h
 *
 *  Header for detection and initialisation of cdrom interface (only) on
 *  ISP16 (MAD16, Mozart) sound card.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/* These are the default values */
#define ISP16_CDROM_TYPE "Sanyo"
#define ISP16_CDROM_IO_BASE 0x340
#define ISP16_CDROM_IRQ 0
#define ISP16_CDROM_DMA 0

/* Some (Media)Magic */
/* define types of drive the interface on an ISP16 card may be looking at */
#define ISP16_DRIVE_X 0x00
#define ISP16_SONY  0x02
#define ISP16_PANASONIC0 0x02
#define ISP16_SANYO0 0x02
#define ISP16_MITSUMI  0x04
#define ISP16_PANASONIC1 0x06
#define ISP16_SANYO1 0x06
#define ISP16_DRIVE_NOT_USED 0x08  /* not used */
#define ISP16_DRIVE_SET_MASK 0xF1  /* don't change 0-bit or 4-7-bits*/
/* ...for port */
#define ISP16_DRIVE_SET_PORT 0xF8D
/* set io parameters */
#define ISP16_BASE_340  0x00
#define ISP16_BASE_330  0x40
#define ISP16_BASE_360  0x80
#define ISP16_BASE_320  0xC0
#define ISP16_IRQ_X  0x00
#define ISP16_IRQ_5  0x04  /* shouldn't be used to avoid sound card conflicts */
#define ISP16_IRQ_7  0x08  /* shouldn't be used to avoid sound card conflicts */
#define ISP16_IRQ_3  0x0C
#define ISP16_IRQ_9  0x10
#define ISP16_IRQ_10  0x14
#define ISP16_IRQ_11  0x18
#define ISP16_DMA_X  0x03
#define ISP16_DMA_3  0x00
#define ISP16_DMA_5  0x00
#define ISP16_DMA_6  0x01
#define ISP16_DMA_7  0x02
#define ISP16_IO_SET_MASK  0x20  /* don't change 5-bit */
/* ...for port */
#define ISP16_IO_SET_PORT  0xF8E
/* enable the card */
#define ISP16_C928__ENABLE_PORT  0xF90  /* ISP16 with OPTi 82C928 chip */
#define ISP16_C929__ENABLE_PORT  0xF91  /* ISP16 with OPTi 82C929 chip */
#define ISP16_ENABLE_CDROM  0x80  /* seven bit */

/* the magic stuff */
#define ISP16_CTRL_PORT  0xF8F
#define ISP16_C928__CTRL  0xE2  /* ISP16 with OPTi 82C928 chip */
#define ISP16_C929__CTRL  0xE3  /* ISP16 with OPTi 82C929 chip */

#define ISP16_IO_BASE 0xF8D
#define ISP16_IO_SIZE 5  /* ports used from 0xF8D up to 0xF91 */
