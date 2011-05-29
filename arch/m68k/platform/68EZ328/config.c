/***************************************************************************/

/*
 *  linux/arch/m68knommu/platform/68EZ328/config.c
 *
 *  Copyright (C) 1993 Hamish Macdonald
 *  Copyright (C) 1999 D. Jeff Dionne
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/***************************************************************************/

#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/machdep.h>
#include <asm/MC68EZ328.h>
#ifdef CONFIG_UCSIMM
#include <asm/bootstd.h>
#endif

/***************************************************************************/

void m68328_timer_gettod(int *year, int *mon, int *day, int *hour, int *min, int *sec);

/***************************************************************************/

void m68ez328_reset(void)
{
  local_irq_disable();
  asm volatile (
    "moveal #0x10c00000, %a0;\n"
    "moveb #0, 0xFFFFF300;\n"
    "moveal 0(%a0), %sp;\n"
    "moveal 4(%a0), %a0;\n"
    "jmp (%a0);\n"
    );
}

/***************************************************************************/

unsigned char *cs8900a_hwaddr;
static int errno;

#ifdef CONFIG_UCSIMM
_bsc0(char *, getserialnum)
_bsc1(unsigned char *, gethwaddr, int, a)
_bsc1(char *, getbenv, char *, a)
#endif

void config_BSP(char *command, int len)
{
  unsigned char *p;

  printk(KERN_INFO "\n68EZ328 DragonBallEZ support (C) 1999 Rt-Control, Inc\n");

#ifdef CONFIG_UCSIMM
  printk(KERN_INFO "uCsimm serial string [%s]\n",getserialnum());
  p = cs8900a_hwaddr = gethwaddr(0);
  printk(KERN_INFO "uCsimm hwaddr %.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n",
         p[0], p[1], p[2], p[3], p[4], p[5]);

  p = getbenv("APPEND");
  if (p) strcpy(p,command);
  else command[0] = 0;
#endif
 
  mach_gettod = m68328_timer_gettod;
  mach_reset = m68ez328_reset;
}

/***************************************************************************/
