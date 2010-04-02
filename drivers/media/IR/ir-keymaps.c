/*
    Keytables for supported remote controls, used on drivers/media
    devices.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#define IR_KEYMAPS

/*
 * NOTICE FOR DEVELOPERS:
 *   The IR mappings should be as close as possible to what's
 *   specified at:
 *      http://linuxtv.org/wiki/index.php/Remote_Controllers
 *
 * The usage of tables with just the command part is deprecated.
 * All new IR keytables should contain address+command and need
 * to define the proper IR_TYPE (IR_TYPE_RC5/IR_TYPE_NEC).
 * The deprecated tables should use IR_TYPE_UNKNOWN
 */
#include <linux/module.h>

#include <linux/input.h>
#include <media/ir-common.h>

/*
 * All keytables got moved to include/media/keytables directory.
 * This file is still needed - at least for now, as their data is
 * dynamically inserted here by the media/ir-common.h, due to the
 * #define IR_KEYMAPS line, at the beginning of this file. The
 * plans are to get rid of this file completely in a near future.
 */

