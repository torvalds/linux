/*
 *
 * BRIEF MODULE DESCRIPTION
 *	Cogent CSB250 board setup
 *
 * Copyright 2002 Cogent Computer Systems, Inc.
 * 	dan@embeddededge.com
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
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/bootmem.h>
#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>

int prom_argc;
char **prom_argv, **prom_envp;
extern void  __init prom_init_cmdline(void);
extern char *prom_getenv(char *envname);

/* When we get initrd working someday.........
*/
int	my_initrd_start, my_initrd_size;

/* Start arguments and environment.
*/
static char	*csb_env[2];
static char	*csb_arg[4];
static char	*arg1 = "console=ttyS3,38400";
static char	*arg2 = "root=/dev/nfs rw ip=any";
static char	*env1 = "ethaddr=00:30:23:50:00:00";

const char *get_system_type(void)
{
	return "Cogent CSB250";
}

int __init prom_init(int argc, char **argv, char **envp, int *prom_vec)
{
	unsigned char *memsize_str;
	unsigned long memsize;

	/* We use a0 and a1 to pass initrd start and size.
	*/
	if (((uint) argc > 0) && ((uint)argv > 0)) {
		my_initrd_start = (uint)argc;
		my_initrd_size = (uint)argv;
	}

	/* First argv is ignored.
	*/
	prom_argc = 3;
	prom_argv = csb_arg;
	prom_envp = csb_env;
	csb_arg[1] = arg1;
	csb_arg[2] = arg2;
	csb_env[0] = env1;

	mips_machgroup = MACH_GROUP_ALCHEMY;
	mips_machtype = MACH_CSB250;

	prom_init_cmdline();
	memsize_str = prom_getenv("memsize");
	if (!memsize_str) {
		memsize = 0x02000000;
	} else {
		memsize = simple_strtol(memsize_str, NULL, 0);
	}
	add_memory_region(0, memsize, BOOT_MEM_RAM);
	return 0;
}
