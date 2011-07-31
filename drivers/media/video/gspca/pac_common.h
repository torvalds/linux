/*
 * Pixart PAC207BCA / PAC73xx common functions
 *
 * Copyright (C) 2008 Hans de Goede <j.w.r.degoede@hhs.nl>
 * Copyright (C) 2005 Thomas Kaiser thomas@kaiser-linux.li
 * Copyleft (C) 2005 Michel Xhaard mxhaard@magic.fr
 *
 * V4L2 by Jean-Francois Moine <http://moinejf.free.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/* We calculate the autogain at the end of the transfer of a frame, at this
   moment a frame with the old settings is being captured and transmitted. So
   if we adjust the gain or exposure we must ignore atleast the next frame for
   the new settings to come into effect before doing any other adjustments. */
#define PAC_AUTOGAIN_IGNORE_FRAMES	2

static const unsigned char pac_sof_marker[5] =
		{ 0xff, 0xff, 0x00, 0xff, 0x96 };

/*
   The following state machine finds the SOF marker sequence
   0xff, 0xff, 0x00, 0xff, 0x96 in a byte stream.

	   +----------+
	   | 0: START |<---------------\
	   +----------+<-\             |
	     |       \---/otherwise    |
	     v 0xff                    |
	   +----------+ otherwise      |
	   |     1    |--------------->*
	   |          |                ^
	   +----------+                |
	     |                         |
	     v 0xff                    |
	   +----------+<-\0xff         |
	/->|          |--/             |
	|  |     2    |--------------->*
	|  |          | otherwise      ^
	|  +----------+                |
	|    |                         |
	|    v 0x00                    |
	|  +----------+                |
	|  |     3    |                |
	|  |          |--------------->*
	|  +----------+ otherwise      ^
	|    |                         |
   0xff |    v 0xff                    |
	|  +----------+                |
	\--|     4    |                |
	   |          |----------------/
	   +----------+ otherwise
	     |
	     v 0x96
	   +----------+
	   |  FOUND   |
	   +----------+
*/

static unsigned char *pac_find_sof(u8 *sof_read,
					unsigned char *m, int len)
{
	int i;

	/* Search for the SOF marker (fixed part) in the header */
	for (i = 0; i < len; i++) {
		switch (*sof_read) {
		case 0:
			if (m[i] == 0xff)
				*sof_read = 1;
			break;
		case 1:
			if (m[i] == 0xff)
				*sof_read = 2;
			else
				*sof_read = 0;
			break;
		case 2:
			switch (m[i]) {
			case 0x00:
				*sof_read = 3;
				break;
			case 0xff:
				/* stay in this state */
				break;
			default:
				*sof_read = 0;
			}
			break;
		case 3:
			if (m[i] == 0xff)
				*sof_read = 4;
			else
				*sof_read = 0;
			break;
		case 4:
			switch (m[i]) {
			case 0x96:
				/* Pattern found */
				PDEBUG(D_FRAM,
					"SOF found, bytes to analyze: %u."
					" Frame starts at byte #%u",
					len, i + 1);
				*sof_read = 0;
				return m + i + 1;
				break;
			case 0xff:
				*sof_read = 2;
				break;
			default:
				*sof_read = 0;
			}
			break;
		default:
			*sof_read = 0;
		}
	}

	return NULL;
}
