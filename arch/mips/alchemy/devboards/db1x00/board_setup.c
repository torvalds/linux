/*
 *
 * BRIEF MODULE DESCRIPTION
 *	Alchemy Db1x00 board setup.
 *
 * Copyright 2000, 2008 MontaVista Software Inc.
 * Author: MontaVista Software, Inc. <source@mvista.com>
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

#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pm.h>

#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1xxx_eth.h>
#include <asm/mach-db1x00/db1x00.h>
#include <asm/mach-db1x00/bcsr.h>
#include <asm/reboot.h>

#include <prom.h>

#ifdef CONFIG_MIPS_BOSPORUS
char irq_tab_alchemy[][5] __initdata = {
	[11] = { -1, AU1500_PCI_INTA, AU1500_PCI_INTB, 0xff, 0xff }, /* IDSEL 11 - miniPCI  */
	[12] = { -1, AU1500_PCI_INTA, 0xff, 0xff, 0xff }, /* IDSEL 12 - SN1741   */
	[13] = { -1, AU1500_PCI_INTA, AU1500_PCI_INTB, AU1500_PCI_INTC, AU1500_PCI_INTD }, /* IDSEL 13 - PCI slot */
};

/*
 * Micrel/Kendin 5 port switch attached to MAC0,
 * MAC0 is associated with PHY address 5 (== WAN port)
 * MAC1 is not associated with any PHY, since it's connected directly
 * to the switch.
 * no interrupts are used
 */
static struct au1000_eth_platform_data eth0_pdata = {
	.phy_static_config	= 1,
	.phy_addr		= 5,
};

static void bosporus_power_off(void)
{
	while (1)
		asm volatile (".set mips3 ; wait ; .set mips0");
}

const char *get_system_type(void)
{
	return "Alchemy Bosporus Gateway Reference";
}
#endif


#ifdef CONFIG_MIPS_MIRAGE
static void mirage_power_off(void)
{
	alchemy_gpio_direction_output(210, 1);
}

const char *get_system_type(void)
{
	return "Alchemy Mirage";
}
#endif


#if defined(CONFIG_MIPS_BOSPORUS) || defined(CONFIG_MIPS_MIRAGE)
static void mips_softreset(void)
{
	asm volatile ("jr\t%0" : : "r"(0xbfc00000));
}

#else

const char *get_system_type(void)
{
	return "Alchemy Db1x00";
}
#endif


void __init board_setup(void)
{
	unsigned long bcsr1, bcsr2;

	bcsr1 = DB1000_BCSR_PHYS_ADDR;
	bcsr2 = DB1000_BCSR_PHYS_ADDR + DB1000_BCSR_HEXLED_OFS;

#ifdef CONFIG_MIPS_DB1000
	printk(KERN_INFO "AMD Alchemy Au1000/Db1000 Board\n");
#endif
#ifdef CONFIG_MIPS_DB1500
	printk(KERN_INFO "AMD Alchemy Au1500/Db1500 Board\n");
#endif
#ifdef CONFIG_MIPS_DB1100
	printk(KERN_INFO "AMD Alchemy Au1100/Db1100 Board\n");
#endif
#ifdef CONFIG_MIPS_BOSPORUS
	au1xxx_override_eth_cfg(0, &eth0_pdata);

	printk(KERN_INFO "AMD Alchemy Bosporus Board\n");
#endif
#ifdef CONFIG_MIPS_MIRAGE
	printk(KERN_INFO "AMD Alchemy Mirage Board\n");
#endif
#ifdef CONFIG_MIPS_DB1550
	printk(KERN_INFO "AMD Alchemy Au1550/Db1550 Board\n");

	bcsr1 = DB1550_BCSR_PHYS_ADDR;
	bcsr2 = DB1550_BCSR_PHYS_ADDR + DB1550_BCSR_HEXLED_OFS;
#endif

	/* initialize board register space */
	bcsr_init(bcsr1, bcsr2);

#if defined(CONFIG_IRDA) && defined(CONFIG_AU1000_FIR)
	{
		u32 pin_func;

		/* Set IRFIRSEL instead of GPIO15 */
		pin_func = au_readl(SYS_PINFUNC) | SYS_PF_IRF;
		au_writel(pin_func, SYS_PINFUNC);
		/* Power off until the driver is in use */
		bcsr_mod(BCSR_RESETS, BCSR_RESETS_IRDA_MODE_MASK,
			 BCSR_RESETS_IRDA_MODE_OFF);
	}
#endif
	bcsr_write(BCSR_PCMCIA, 0);	/* turn off PCMCIA power */

	/* Enable GPIO[31:0] inputs */
	alchemy_gpio1_input_enable();

#ifdef CONFIG_MIPS_MIRAGE
	{
		u32 pin_func;

		/* GPIO[20] is output */
		alchemy_gpio_direction_output(20, 0);

		/* Set GPIO[210:208] instead of SSI_0 */
		pin_func = au_readl(SYS_PINFUNC) | SYS_PF_S0;

		/* Set GPIO[215:211] for LEDs */
		pin_func |= 5 << 2;

		/* Set GPIO[214:213] for more LEDs */
		pin_func |= 5 << 12;

		/* Set GPIO[207:200] instead of PCMCIA/LCD */
		pin_func |= SYS_PF_LCD | SYS_PF_PC;
		au_writel(pin_func, SYS_PINFUNC);

		/*
		 * Enable speaker amplifier.  This should
		 * be part of the audio driver.
		 */
		alchemy_gpio_direction_output(209, 1);

		pm_power_off = mirage_power_off;
		_machine_halt = mirage_power_off;
		_machine_restart = (void(*)(char *))mips_softreset;
	}
#endif

#ifdef CONFIG_MIPS_BOSPORUS
	pm_power_off = bosporus_power_off;
	_machine_halt = bosporus_power_off;
	_machine_restart = (void(*)(char *))mips_softreset;
#endif
	au_sync();
}

static int __init db1x00_init_irq(void)
{
#if defined(CONFIG_MIPS_MIRAGE)
	irq_set_irq_type(AU1500_GPIO7_INT, IRQF_TRIGGER_RISING); /* TS pendown */
#elif defined(CONFIG_MIPS_DB1550)
	irq_set_irq_type(AU1550_GPIO0_INT, IRQF_TRIGGER_LOW);  /* CD0# */
	irq_set_irq_type(AU1550_GPIO1_INT, IRQF_TRIGGER_LOW);  /* CD1# */
	irq_set_irq_type(AU1550_GPIO3_INT, IRQF_TRIGGER_LOW);  /* CARD0# */
	irq_set_irq_type(AU1550_GPIO5_INT, IRQF_TRIGGER_LOW);  /* CARD1# */
	irq_set_irq_type(AU1550_GPIO21_INT, IRQF_TRIGGER_LOW); /* STSCHG0# */
	irq_set_irq_type(AU1550_GPIO22_INT, IRQF_TRIGGER_LOW); /* STSCHG1# */
#elif defined(CONFIG_MIPS_DB1500)
	irq_set_irq_type(AU1500_GPIO0_INT, IRQF_TRIGGER_LOW); /* CD0# */
	irq_set_irq_type(AU1500_GPIO3_INT, IRQF_TRIGGER_LOW); /* CD1# */
	irq_set_irq_type(AU1500_GPIO2_INT, IRQF_TRIGGER_LOW); /* CARD0# */
	irq_set_irq_type(AU1500_GPIO5_INT, IRQF_TRIGGER_LOW); /* CARD1# */
	irq_set_irq_type(AU1500_GPIO1_INT, IRQF_TRIGGER_LOW); /* STSCHG0# */
	irq_set_irq_type(AU1500_GPIO4_INT, IRQF_TRIGGER_LOW); /* STSCHG1# */
#elif defined(CONFIG_MIPS_DB1100)
	irq_set_irq_type(AU1100_GPIO0_INT, IRQF_TRIGGER_LOW); /* CD0# */
	irq_set_irq_type(AU1100_GPIO3_INT, IRQF_TRIGGER_LOW); /* CD1# */
	irq_set_irq_type(AU1100_GPIO2_INT, IRQF_TRIGGER_LOW); /* CARD0# */
	irq_set_irq_type(AU1100_GPIO5_INT, IRQF_TRIGGER_LOW); /* CARD1# */
	irq_set_irq_type(AU1100_GPIO1_INT, IRQF_TRIGGER_LOW); /* STSCHG0# */
	irq_set_irq_type(AU1100_GPIO4_INT, IRQF_TRIGGER_LOW); /* STSCHG1# */
#elif defined(CONFIG_MIPS_DB1000)
	irq_set_irq_type(AU1000_GPIO0_INT, IRQF_TRIGGER_LOW); /* CD0# */
	irq_set_irq_type(AU1000_GPIO3_INT, IRQF_TRIGGER_LOW); /* CD1# */
	irq_set_irq_type(AU1000_GPIO2_INT, IRQF_TRIGGER_LOW); /* CARD0# */
	irq_set_irq_type(AU1000_GPIO5_INT, IRQF_TRIGGER_LOW); /* CARD1# */
	irq_set_irq_type(AU1000_GPIO1_INT, IRQF_TRIGGER_LOW); /* STSCHG0# */
	irq_set_irq_type(AU1000_GPIO4_INT, IRQF_TRIGGER_LOW); /* STSCHG1# */
#endif
	return 0;
}
arch_initcall(db1x00_init_irq);
