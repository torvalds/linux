/*
 *  Copyright (C) 2004 Florian Schirmer <jolt@tuxbox.org>
 *  Copyright (C) 2007 Aurelien Jarno <aurelien@aurel32.net>
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

#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <asm/bootinfo.h>
#include <asm/fw/cfe/cfe_api.h>
#include <asm/fw/cfe/cfe_error.h>

static int cfe_cons_handle;

const char *get_system_type(void)
{
	return "Broadcom BCM47XX";
}

void prom_putchar(char c)
{
	while (cfe_write(cfe_cons_handle, &c, 1) == 0)
		;
}

static __init void prom_init_cfe(void)
{
	uint32_t cfe_ept;
	uint32_t cfe_handle;
	uint32_t cfe_eptseal;
	int argc = fw_arg0;
	char **envp = (char **) fw_arg2;
	int *prom_vec = (int *) fw_arg3;

	/*
	 * Check if a loader was used; if NOT, the 4 arguments are
	 * what CFE gives us (handle, 0, EPT and EPTSEAL)
	 */
	if (argc < 0) {
		cfe_handle = (uint32_t)argc;
		cfe_ept = (uint32_t)envp;
		cfe_eptseal = (uint32_t)prom_vec;
	} else {
		if ((int)prom_vec < 0) {
			/*
			 * Old loader; all it gives us is the handle,
			 * so use the "known" entrypoint and assume
			 * the seal.
			 */
			cfe_handle = (uint32_t)prom_vec;
			cfe_ept = 0xBFC00500;
			cfe_eptseal = CFE_EPTSEAL;
		} else {
			/*
			 * Newer loaders bundle the handle/ept/eptseal
			 * Note: prom_vec is in the loader's useg
			 * which is still alive in the TLB.
			 */
			cfe_handle = prom_vec[0];
			cfe_ept = prom_vec[2];
			cfe_eptseal = prom_vec[3];
		}
	}

	if (cfe_eptseal != CFE_EPTSEAL) {
		/* too early for panic to do any good */
		printk(KERN_ERR "CFE's entrypoint seal doesn't match.");
		while (1) ;
	}

	cfe_init(cfe_handle, cfe_ept);
}

static __init void prom_init_console(void)
{
	/* Initialize CFE console */
	cfe_cons_handle = cfe_getstdhandle(CFE_STDHANDLE_CONSOLE);
}

static __init void prom_init_cmdline(void)
{
	static char buf[COMMAND_LINE_SIZE] __initdata;

	/* Get the kernel command line from CFE */
	if (cfe_getenv("LINUX_CMDLINE", buf, COMMAND_LINE_SIZE) >= 0) {
		buf[COMMAND_LINE_SIZE - 1] = 0;
		strcpy(arcs_cmdline, buf);
	}

	/* Force a console handover by adding a console= argument if needed,
	 * as CFE is not available anymore later in the boot process. */
	if ((strstr(arcs_cmdline, "console=")) == NULL) {
		/* Try to read the default serial port used by CFE */
		if ((cfe_getenv("BOOT_CONSOLE", buf, COMMAND_LINE_SIZE) < 0)
		    || (strncmp("uart", buf, 4)))
			/* Default to uart0 */
			strcpy(buf, "uart0");

		/* Compute the new command line */
		snprintf(arcs_cmdline, COMMAND_LINE_SIZE, "%s console=ttyS%c,115200",
			 arcs_cmdline, buf[4]);
	}
}

static __init void prom_init_mem(void)
{
	unsigned long mem;

	/* Figure out memory size by finding aliases.
	 *
	 * We should theoretically use the mapping from CFE using cfe_enummem().
	 * However as the BCM47XX is mostly used on low-memory systems, we
	 * want to reuse the memory used by CFE (around 4MB). That means cfe_*
	 * functions stop to work at some point during the boot, we should only
	 * call them at the beginning of the boot.
	 */
	for (mem = (1 << 20); mem < (128 << 20); mem += (1 << 20)) {
		if (*(unsigned long *)((unsigned long)(prom_init) + mem) ==
		    *(unsigned long *)(prom_init))
			break;
	}

	add_memory_region(0, mem, BOOT_MEM_RAM);
}

void __init prom_init(void)
{
	prom_init_cfe();
	prom_init_console();
	prom_init_cmdline();
	prom_init_mem();
}

void __init prom_free_prom_memory(void)
{
}

