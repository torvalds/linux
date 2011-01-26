/*.
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 * Reset the PNX8550 board.
 *
 */
#include <linux/kernel.h>

#include <asm/processor.h>
#include <asm/reboot.h>
#include <glb.h>

void pnx8550_machine_restart(char *command)
{
	PNX8550_RST_CTL = PNX8550_RST_DO_SW_RST;
}

void pnx8550_machine_halt(void)
{
	while (1) {
		if (cpu_wait)
			cpu_wait();
	}
}
